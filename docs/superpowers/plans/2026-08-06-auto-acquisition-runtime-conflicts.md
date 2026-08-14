# Auto Acquisition Runtime Conflicts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the remaining runtime conflicts between manual acquisition, auto acquisition, hardware pulse startup/reuse, auto exposure, ROI relocalization, and repeated pulse-board timeout status messages.

**Architecture:** Keep manual and auto acquisition on the same `DIMM::onStartCapture()` / `DIMM::onStopCapture()` path. Add narrow state guards around auto-window manual suppression, pulse reuse validation, auto-exposure-induced ROI relocalization, and pulse-board timeout status throttling. Do not create a second acquisition pipeline.

**Tech Stack:** C++17, Qt 6 Widgets, existing DIMM runtime classes, Python `unittest` static tests.

---

## Hard Constraints

- Do **not** run CMake, MSBuild, Visual Studio build, or package/deploy commands.
- Verification for this plan is static Python tests only. The human operator will build later.
- Do not refactor unrelated acquisition, ROI, alignment, or auto-exposure code.
- Do not rewrite unrelated Chinese UI strings. Some terminal output may show mojibake; preserve existing source text encoding.
- Do not change automatic acquisition start/stop policy: if current time is inside the configured window, auto acquisition may start even if the exe was opened after the window start time.
- Auto acquisition must continue to call `onStartCapture()` and `onStopCapture()` instead of directly touching cameras, pulse generator, result files, or ROI helpers.

## Problem Summary

Four remaining risk points must be addressed:

1. If auto acquisition is enabled and the current time is inside the auto window, a user can manually start acquisition and then manually stop it. Current suppression only records stops for runs that were originally auto-started. The scheduler may therefore start acquisition again in the same window.
2. In hardware trigger mode, `onStartCapture()` currently reuses any running pulse output. It should only reuse a pulse output whose config matches the full-frame localization pulse, especially the `2 Hz` localization frequency.
3. Auto exposure does not directly start/stop acquisition, but an exposure adjustment can briefly destabilize centroid quality. That transient should not immediately force full-frame ROI relocalization.
4. Pulse-board ACK timeout is already tolerated when frames arrive, but timeout-related status messages can still repeat from multiple paths. Use a common timeout detector and throttled status helper so the UI reports the condition without drowning normal acquisition state.

## Files To Modify

- Modify `src/DIMM.h`
  - Add helper declarations and small runtime state members.
- Modify `src/DIMM.cpp`
  - Update manual suppression logic.
  - Use pulse reuse helper from hardware-trigger startup.
  - Add pulse-board timeout status helper implementation if placed here.
- Modify `src/DIMM.LiveRoi.cpp`
  - Add pulse config matching helper implementation if placed here.
  - Use timeout detector/throttled status in ROI pulse switch path.
  - Add auto-exposure grace check in ROI centroid-loss path.
- Modify `src/DIMM.CommCamera.cpp`
  - Use timeout detector/throttled status in live frame path.
- Modify `src/DIMM.AutoExposure.cpp`
  - Add auto-exposure ROI relocalization grace helper implementation.
- Create `tests/test_auto_acquisition_runtime_conflicts_static.py`
  - Static regression tests for all four behaviors.

Do not modify `CMakeLists.txt` for this plan.

---

## Task 1: Add Static Regression Tests

**Files:**
- Create: `tests/test_auto_acquisition_runtime_conflicts_static.py`

- [ ] **Step 1: Create the failing static test file**

Create `tests/test_auto_acquisition_runtime_conflicts_static.py` with this exact content:

```python
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class AutoAcquisitionRuntimeConflictStaticTest(unittest.TestCase):
    def test_manual_stop_inside_auto_window_suppresses_current_window(self):
        cpp = read("src/DIMM.cpp")
        body = cpp.split("void DIMM::noteManualAutoAcquisitionStopIfNeeded()", 1)[1].split(
            "bool DIMM::shouldRetryFailedLiveStartup() const",
            1,
        )[0]

        self.assertIn("m_autoAcquisitionCommandInProgress", body)
        self.assertIn("AutoAcquisitionScheduler::resolveWindow", body)
        self.assertIn("AutoAcquisitionScheduler::contains", body)
        self.assertIn("m_autoAcquisitionSuppressedWindowId = suppressedWindowId", body)
        self.assertIn("m_autoAcquisitionStartedCurrentRun = false", body)
        self.assertIn("m_autoAcquisitionActiveWindowId.clear()", body)

    def test_hardware_start_reuses_only_full_frame_localization_pulse(self):
        header = read("src/DIMM.h")
        self.assertIn("bool isFullFrameLocalizationPulseRunning() const", header)

        cpp = read("src/DIMM.cpp")
        start_body = cpp.split("void DIMM::onStartCapture()", 1)[1].split(
            "void DIMM::onStopCapture()",
            1,
        )[0]
        self.assertIn("isFullFrameLocalizationPulseRunning()", start_body)
        self.assertNotIn("m_pulseGenerator->isRunning();\n            if (reuseRunningPulse)", start_body)

        live_roi = read("src/DIMM.LiveRoi.cpp")
        helper_body = live_roi.split("bool DIMM::isFullFrameLocalizationPulseRunning() const", 1)[1].split(
            "bool DIMM::commitPairedInitialRoisIfReady()",
            1,
        )[0]
        self.assertIn("kFullFrameLocalizationPulseHz", helper_body)
        self.assertIn("pulseConfigsMatch", helper_body)
        self.assertIn("m_pulseGenerator->config()", helper_body)

    def test_auto_exposure_adjustment_defers_roi_relocalization(self):
        header = read("src/DIMM.h")
        self.assertIn("bool isAutoExposureRoiRelocalizationGraceActive(qint64 nowMs) const", header)
        self.assertIn("kAutoExposureRoiRelocalizationGraceMs", header)

        auto_exposure = read("src/DIMM.AutoExposure.cpp")
        self.assertIn("bool DIMM::isAutoExposureRoiRelocalizationGraceActive(qint64 nowMs) const", auto_exposure)
        self.assertIn("m_lastAutoExposureAdjustMs", auto_exposure)
        self.assertIn("kAutoExposureRoiRelocalizationGraceMs", auto_exposure)

        live_roi = read("src/DIMM.LiveRoi.cpp")
        loss_body = live_roi.split("void DIMM::handleLiveRoiCentroidLoss", 1)[1].split(
            "bool DIMM::validateAndCacheLiveRoiCapabilities",
            1,
        )[0]
        self.assertIn("isAutoExposureRoiRelocalizationGraceActive(nowMs)", loss_body)
        self.assertLess(
            loss_body.find("isAutoExposureRoiRelocalizationGraceActive(nowMs)"),
            loss_body.find("requestLiveFullFrameRelocalization"),
        )

    def test_pulse_board_timeout_uses_common_detector_and_throttled_status(self):
        header = read("src/DIMM.h")
        self.assertIn("bool isPulseBoardResponseTimeout(const QString& reason) const", header)
        self.assertIn("void setPulseBoardResponseTimeoutStatus", header)
        self.assertIn("m_lastPulseBoardTimeoutStatusMs", header)

        cpp = read("src/DIMM.cpp")
        self.assertIn("bool DIMM::isPulseBoardResponseTimeout(const QString& reason) const", cpp)
        self.assertIn("void DIMM::setPulseBoardResponseTimeoutStatus", cpp)
        self.assertIn("kPulseBoardTimeoutStatusThrottleMs", cpp + header)

        start_body = cpp.split("void DIMM::onStartCapture()", 1)[1].split(
            "void DIMM::onStopCapture()",
            1,
        )[0]
        self.assertIn("isPulseBoardResponseTimeout(reason)", start_body)
        self.assertIn("setPulseBoardResponseTimeoutStatus", start_body)

        comm = read("src/DIMM.CommCamera.cpp")
        live_body = comm.split("void DIMM::handleLiveFramePacket", 1)[1].split(
            "void DIMM::scheduleHardwareTriggerStartupCheck",
            1,
        )[0]
        self.assertIn("setPulseBoardResponseTimeoutStatus", live_body)

        live_roi = read("src/DIMM.LiveRoi.cpp")
        commit_body = live_roi.split("bool DIMM::commitPairedInitialRoisIfReady()", 1)[1].split(
            "bool DIMM::startHardwarePulseStage",
            1,
        )[0]
        self.assertIn("isPulseBoardResponseTimeout(reason)", commit_body)
        self.assertIn("setPulseBoardResponseTimeoutStatus", commit_body)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the new test and verify it fails**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_runtime_conflicts_static
```

Expected result before implementation:

```text
FAILED
```

It should fail because the helper methods and new suppression logic do not exist yet.

---

## Task 2: Suppress Auto Restart After Manual Stop Inside Current Auto Window

**Files:**
- Modify: `src/DIMM.cpp`
- Test: `tests/test_auto_acquisition_runtime_conflicts_static.py`

**Behavior Required:**

If auto acquisition is enabled and the current time is inside the active auto window, any user manual stop should suppress auto acquisition for that window, even if the current Live run was started manually.

Automatic scheduled stop must not create suppression. Hardware startup recovery stop must not create suppression. Those internal operations are already protected by `m_autoAcquisitionCommandInProgress`; preserve that guard.

- [ ] **Step 1: Replace `noteManualAutoAcquisitionStopIfNeeded()`**

In `src/DIMM.cpp`, replace the full body of `DIMM::noteManualAutoAcquisitionStopIfNeeded()` with:

```cpp
void DIMM::noteManualAutoAcquisitionStopIfNeeded()
{
    if (m_autoAcquisitionCommandInProgress) {
        return;
    }

    QString suppressedWindowId;
    bool shouldSuppress = false;

    if (m_autoAcquisitionStartedCurrentRun &&
        !m_autoAcquisitionActiveWindowId.isEmpty()) {
        suppressedWindowId = m_autoAcquisitionActiveWindowId;
        shouldSuppress = true;
    } else if (m_autoAcquisitionConfig.enabled) {
        const QDateTime now = QDateTime::currentDateTime();
        const AutoAcquisitionWindow window =
            AutoAcquisitionScheduler::resolveWindow(
                m_autoAcquisitionConfig,
                now);
        if (window.valid &&
            AutoAcquisitionScheduler::contains(window, now)) {
            suppressedWindowId = window.windowId;
            shouldSuppress = true;
        }
    }

    if (!shouldSuppress || suppressedWindowId.isEmpty()) {
        return;
    }

    m_autoAcquisitionSuppressedWindowId = suppressedWindowId;
    m_autoAcquisitionStartedCurrentRun = false;
    m_autoAcquisitionActiveWindowId.clear();
    setAutoAcquisitionStatus(QStringLiteral("自动采集已手动停止，本观测窗口不再自动重启"),
                             UiStatusLevel::Warning,
                             QStringLiteral("manual-stop-suppression"));
}
```

- [ ] **Step 2: Verify the new focused test partially passes this task**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_runtime_conflicts_static.AutoAcquisitionRuntimeConflictStaticTest.test_manual_stop_inside_auto_window_suppresses_current_window
```

Expected result:

```text
OK
```

- [ ] **Step 3: Run existing auto-acquisition DIMM static tests**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_dimm_static
```

Expected result:

```text
OK
```

---

## Task 3: Reuse Running Pulse Only When It Matches Full-Frame Localization

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.cpp`
- Modify: `src/DIMM.LiveRoi.cpp`
- Test: `tests/test_auto_acquisition_runtime_conflicts_static.py`

**Behavior Required:**

In hardware trigger mode, live startup begins with full-frame localization. The pulse output may only be reused if it already matches the full-frame localization pulse config, especially `frequencyHz = kFullFrameLocalizationPulseHz`. Otherwise startup must call `startFullFrameLocalizationPulse(&reason)`, which will reconfigure the pulse generator.

- [ ] **Step 1: Declare the helper**

In `src/DIMM.h`, near the existing pulse helper declarations:

```cpp
bool startHardwarePulseStage(double frequencyHz, const QString& stageLabel, QString* reason = nullptr);
bool startFullFrameLocalizationPulse(QString* reason = nullptr);
bool isFullFrameLocalizationPulseRunning() const;
bool switchToRoiTrackingPulse(QString* reason = nullptr);
```

- [ ] **Step 2: Implement the helper**

In `src/DIMM.LiveRoi.cpp`, insert this function immediately before `bool DIMM::commitPairedInitialRoisIfReady()`:

```cpp
bool DIMM::isFullFrameLocalizationPulseRunning() const
{
    if (m_configTriggerMode == 0 ||
        !m_pulseGeneratorEnabled ||
        !m_pulseGenerator ||
        !m_pulseGenerator->isRunning()) {
        return false;
    }

    PulseGeneratorManager::Config pulseConfig;
    pulseConfig.enabled = true;
    pulseConfig.portName = m_pulseGeneratorPort;
    pulseConfig.baudRate = m_pulseGeneratorBaudRate;
    pulseConfig.terminalId = m_pulseGeneratorTerminalId;
    pulseConfig.frequencyHz = kFullFrameLocalizationPulseHz;
    pulseConfig.pulseCount = m_pulseGeneratorPulseCount;
    pulseConfig.dutyPercent = m_pulseGeneratorDutyPercent;
    pulseConfig.remoteControl = m_pulseGeneratorRemoteControl;

    return pulseConfigsMatch(m_pulseGenerator->config(), pulseConfig);
}
```

`src/DIMM.LiveRoi.cpp` already includes `PulseGeneratorManager.h` and `DimmRuntimeHelpers.h`. Do not add duplicate includes unless the local file no longer has them.

- [ ] **Step 3: Use the helper in `onStartCapture()`**

In `src/DIMM.cpp`, inside the hardware trigger branch of `DIMM::onStartCapture()`, replace:

```cpp
const bool reuseRunningPulse =
    m_pulseGeneratorEnabled && m_pulseGenerator && m_pulseGenerator->isRunning();
```

with:

```cpp
const bool reuseRunningPulse = isFullFrameLocalizationPulseRunning();
```

Leave the existing `if (reuseRunningPulse) { ... } else { startFullFrameLocalizationPulse(&reason) ... }` structure intact.

- [ ] **Step 4: Run the focused test**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_runtime_conflicts_static.AutoAcquisitionRuntimeConflictStaticTest.test_hardware_start_reuses_only_full_frame_localization_pulse
```

Expected result:

```text
OK
```

---

## Task 4: Add Auto-Exposure Grace Period Before ROI Relocalization

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.AutoExposure.cpp`
- Modify: `src/DIMM.LiveRoi.cpp`
- Test: `tests/test_auto_acquisition_runtime_conflicts_static.py`

**Behavior Required:**

When automatic exposure changes camera exposure, the next few seconds can contain transient brightness/centroid instability. During that grace period, do not immediately call `requestLiveFullFrameRelocalization()` from centroid-loss handling. This must not hide a long-term real star loss: after the grace period expires, the existing centroid-loss and full-frame relocalization logic must run normally.

- [ ] **Step 1: Add declaration and constant**

In `src/DIMM.h`, near the existing auto-exposure helpers:

```cpp
bool isAutoExposureRoiRelocalizationGraceActive(qint64 nowMs) const;
```

In the private member/constants area near `m_lastAutoExposureAdjustMs`, add:

```cpp
static constexpr int kAutoExposureRoiRelocalizationGraceMs = 3000;
```

Keep the existing `m_lastAutoExposureAdjustMs` member. Do not add a second duplicate timestamp.

- [ ] **Step 2: Implement the helper**

In `src/DIMM.AutoExposure.cpp`, after `DIMM::resetAutoExposureState()` and before `DIMM::autoExposureStateName(...)`, add:

```cpp
bool DIMM::isAutoExposureRoiRelocalizationGraceActive(qint64 nowMs) const
{
    if (!m_autoExposureConfig.enabled ||
        m_lastAutoExposureAdjustMs < 0 ||
        nowMs < m_lastAutoExposureAdjustMs) {
        return false;
    }

    return (nowMs - m_lastAutoExposureAdjustMs) <
           kAutoExposureRoiRelocalizationGraceMs;
}
```

- [ ] **Step 3: Gate ROI centroid-loss relocalization**

In `src/DIMM.LiveRoi.cpp`, find `void DIMM::handleLiveRoiCentroidLoss(int cameraIndex)`.

Near the beginning of the function, after it computes or can compute `nowMs`, add this guard before any call to `requestLiveFullFrameRelocalization(...)`:

```cpp
if (isAutoExposureRoiRelocalizationGraceActive(nowMs)) {
    auto& runtime = activeRuntime();
    if (cameraIndex >= 0 && cameraIndex < 2) {
        runtime.lostCentroidFrameCount[cameraIndex] = 0;
        runtime.lostCentroidSinceMs[cameraIndex] = -1;
    }
    setStatusMessage(QStringLiteral("自动曝光调整后等待 ROI 亮度稳定，暂缓全画幅重定位"),
                     UiStatusLevel::Warning);
    return;
}
```

If the function currently declares `auto& runtime = activeRuntime();` before this point, reuse that variable and do not redeclare it. If it currently declares `nowMs` later, move the `nowMs` declaration earlier instead of calling `QDateTime::currentMSecsSinceEpoch()` twice.

- [ ] **Step 4: Run the focused test**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_runtime_conflicts_static.AutoAcquisitionRuntimeConflictStaticTest.test_auto_exposure_adjustment_defers_roi_relocalization
```

Expected result:

```text
OK
```

---

## Task 5: Centralize And Throttle Pulse-Board Timeout Status

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.cpp`
- Modify: `src/DIMM.LiveRoi.cpp`
- Modify: `src/DIMM.CommCamera.cpp`
- Test: `tests/test_auto_acquisition_runtime_conflicts_static.py`

**Behavior Required:**

Use one helper to detect the pulse-board ACK timeout string and one helper to throttle status updates. The timeout is still a warning, not a hard acquisition failure, when camera frames confirm trigger output.

- [ ] **Step 1: Declare helpers and state**

In `src/DIMM.h`, near other runtime helper declarations, add:

```cpp
bool isPulseBoardResponseTimeout(const QString& reason) const;
void setPulseBoardResponseTimeoutStatus(const QString& text,
                                        UiStatusLevel level = UiStatusLevel::Warning);
```

In the private member area near `m_pulseBoardResponseTimedOut`, add:

```cpp
qint64 m_lastPulseBoardTimeoutStatusMs = -1;
static constexpr int kPulseBoardTimeoutStatusThrottleMs = 10000;
```

- [ ] **Step 2: Implement helpers**

In `src/DIMM.cpp`, place these functions before `DIMM::onStartCapture()`:

```cpp
bool DIMM::isPulseBoardResponseTimeout(const QString& reason) const
{
    return reason.contains(QStringLiteral("Timed out waiting for pulse-board response."),
                           Qt::CaseInsensitive);
}

void DIMM::setPulseBoardResponseTimeoutStatus(const QString& text,
                                              UiStatusLevel level)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastPulseBoardTimeoutStatusMs >= 0 &&
        nowMs - m_lastPulseBoardTimeoutStatusMs <
            kPulseBoardTimeoutStatusThrottleMs) {
        return;
    }

    m_lastPulseBoardTimeoutStatusMs = nowMs;
    setStatusMessage(text, level);
}
```

- [ ] **Step 3: Use detector/helper in startup pulse timeout path**

In `src/DIMM.cpp`, inside `DIMM::onStartCapture()`, replace this pattern:

```cpp
const bool pulseResponseTimeout =
    reason.contains(QStringLiteral("Timed out waiting for pulse-board response."),
                    Qt::CaseInsensitive);
if (pulseResponseTimeout) {
    m_pulseBoardResponseTimedOut = true;

    setStatusMessage(
        QStringLiteral(
            "状态: 脉冲板应答超时，但已继续等待首帧确认硬件触发是否生效"),
        UiStatusLevel::Warning);

    scheduleHardwareTriggerStartupCheck();
    return;
}
```

with:

```cpp
if (isPulseBoardResponseTimeout(reason)) {
    m_pulseBoardResponseTimedOut = true;

    setPulseBoardResponseTimeoutStatus(
        QStringLiteral(
            "状态: 脉冲板应答超时，但已继续等待首帧确认硬件触发是否生效"));

    scheduleHardwareTriggerStartupCheck();
    return;
}
```

- [ ] **Step 4: Use detector/helper in ROI high-frequency pulse switch path**

In `src/DIMM.LiveRoi.cpp`, inside `DIMM::commitPairedInitialRoisIfReady()`, replace:

```cpp
const bool pulseResponseTimeout =
    reason.contains(QStringLiteral("Timed out waiting for pulse-board response."),
                    Qt::CaseInsensitive);
if (!pulseResponseTimeout) {
```

with:

```cpp
if (!isPulseBoardResponseTimeout(reason)) {
```

Then replace the timeout warning status:

```cpp
setStatusMessage(QStringLiteral("状态: 双相机全画幅定位完成，ROI 已写入；脉冲板未返回串口应答，继续用图像帧确认触发输出"),
                 UiStatusLevel::Warning);
```

with:

```cpp
setPulseBoardResponseTimeoutStatus(
    QStringLiteral("状态: 双相机全画幅定位完成，ROI 已写入；脉冲板未返回串口应答，继续用图像帧确认触发输出"));
```

- [ ] **Step 5: Use helper in live frame path**

In `src/DIMM.CommCamera.cpp`, inside `DIMM::handleLiveFramePacket(...)`, replace the timeout status block:

```cpp
if (m_configTriggerMode != 0 &&
    (m_statusText.contains(QStringLiteral("Timed out waiting for pulse-board response."),
                           Qt::CaseInsensitive) ||
     m_statusText.contains(QStringLiteral("脉冲板应答超时")))) {
    setStatusMessage(QStringLiteral("状态: 已收到硬件触发图像帧，脉冲板未返回串口应答但采集继续"),
                     UiStatusLevel::Warning);
}
```

with:

```cpp
if (m_configTriggerMode != 0 &&
    m_pulseBoardResponseTimedOut) {
    setPulseBoardResponseTimeoutStatus(
        QStringLiteral("状态: 已收到硬件触发图像帧，脉冲板未返回串口应答但采集继续"));
}
```

Keep the existing `confirmHardwareTriggerStartupIfReady();` call before this block.

- [ ] **Step 6: Reset timeout throttle at start/stop boundaries**

In `DIMM::onStartCapture()`, where startup state is reset with `m_pulseBoardResponseTimedOut = false;`, also add:

```cpp
m_lastPulseBoardTimeoutStatusMs = -1;
```

In `DIMM::resetLiveStartupRecoveryState(bool resetRetryCount)`, where `m_pulseBoardResponseTimedOut = false;`, also add:

```cpp
m_lastPulseBoardTimeoutStatusMs = -1;
```

In `DIMM::stopLiveCapture()`, where `m_pulseBoardResponseTimedOut = false;`, also add:

```cpp
m_lastPulseBoardTimeoutStatusMs = -1;
```

- [ ] **Step 7: Run the focused test**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_runtime_conflicts_static.AutoAcquisitionRuntimeConflictStaticTest.test_pulse_board_timeout_uses_common_detector_and_throttled_status
```

Expected result:

```text
OK
```

---

## Task 6: Run Final Static Verification Only

**Files:**
- Test only

- [ ] **Step 1: Run the new regression test file**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_runtime_conflicts_static
```

Expected result:

```text
OK
```

- [ ] **Step 2: Run adjacent existing static tests**

Run:

```powershell
python -m unittest tests.test_auto_acquisition_config_static tests.test_auto_acquisition_scheduler_static tests.test_auto_acquisition_settings_static tests.test_auto_acquisition_dimm_static tests.test_dimm_live_roi_cpp_split_static tests.test_dimm_runtime_helpers_split_static tests.test_p1_threading_static
```

Expected result:

```text
OK
```

- [ ] **Step 3: Do not build**

Do not run any of these commands:

```powershell
cmake --build build
msbuild
devenv
windeployqt
deploy.bat
```

The human operator will build in Visual Studio after reviewing the code.

- [ ] **Step 4: Report changed files**

Run:

```powershell
git status --short src\DIMM.h src\DIMM.cpp src\DIMM.LiveRoi.cpp src\DIMM.CommCamera.cpp src\DIMM.AutoExposure.cpp tests\test_auto_acquisition_runtime_conflicts_static.py
```

Expected changed-file set:

```text
M  src/DIMM.h
M  src/DIMM.cpp
M  src/DIMM.LiveRoi.cpp
M  src/DIMM.CommCamera.cpp
M  src/DIMM.AutoExposure.cpp
?? tests/test_auto_acquisition_runtime_conflicts_static.py
```

If `src/DIMM.LiveRoi.cpp` is already untracked in the current workspace, do not assume it is a new file created by this task. Report that separately to the human operator.

---

## Acceptance Checklist

- [ ] Manual stop inside an active auto acquisition window writes `m_autoAcquisitionSuppressedWindowId` even when the run was manually started.
- [ ] Automatic scheduled stop and hardware startup recovery do not write manual suppression because `m_autoAcquisitionCommandInProgress` still guards them.
- [ ] Hardware-trigger startup reuses pulse output only when `pulseConfigsMatch(... kFullFrameLocalizationPulseHz ...)` is true.
- [ ] A mismatched running high-frequency pulse is reconfigured through `startFullFrameLocalizationPulse(&reason)`.
- [ ] Auto-exposure adjustment creates a short ROI relocalization grace period using existing `m_lastAutoExposureAdjustMs`.
- [ ] After the grace period expires, existing ROI centroid-loss relocalization behavior remains unchanged.
- [ ] Pulse-board timeout detection uses `isPulseBoardResponseTimeout(...)`.
- [ ] Repeated pulse-board timeout UI updates use `setPulseBoardResponseTimeoutStatus(...)`.
- [ ] No build command was run.

