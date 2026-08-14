# Auto Exposure State Machine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a configurable state-machine auto-exposure protector that keeps DIMM acquisition, `r0/seeing` calculation, and result saving continuous while preventing ROI star spots from becoming saturated or lost in noise.

**Architecture:** Add a side-channel auto-exposure metric stream from ROI processing, feed it into a two-camera common-trend state machine, and perform online exposure/template changes without stopping capture. Keep the existing image processing and result saving pipeline running; automatic exposure only observes metrics and updates camera exposure/template metadata.

**Tech Stack:** C++17, Qt signals/slots, OpenCV `cv::Mat`, Galaxy SDK manual exposure control, existing CSV `ResultWriter`, standalone Python 3 verification script.

## Global Constraints

- Do not modify `CMakeLists.txt` or generated build files.
- Do not add a new `.cpp` file that requires CMake registration.
- Do not use camera SDK `ExposureAuto` for formal measurement.
- Do not stop, restart, or pause acquisition for auto exposure.
- Do not drop frames during auto exposure adjustment.
- Auto-exposure adjustment frames must continue into `r0/seeing` calculation and result saving.
- Both cameras must always use the same exposure value.
- Both cameras must always use the same hot-pixel template exposure level.
- Auto-exposure decisions must use the shared trend of both cameras.
- If camera trends conflict, hold exposure and report `TREND_CONFLICT`.
- If the target star is not observable at maximum exposure, report `WEATHER_TOO_DARK / STAR_LOST`.
- Keep all key thresholds, windows, cooldowns, and step limits configurable from settings.
- Preserve Mono12/raw DN computation assumptions.

---

## File Structure

- Modify: `src/AppConfig.h`
  - Extend `AutoExposureConfig` with state-machine thresholds, windows, cooldown, step limits, and two-camera agreement parameters.
- Modify: `src/SettingsDialog.h`
  - Change the auto-exposure callback to pass `const AutoExposureConfig&`.
  - Add UI fields for every new auto-exposure parameter.
- Modify: `src/SettingsDialog.cpp`
  - Add form rows, parsing, validation, and config construction for the new parameters.
- Create: `src/AutoExposureController.h`
  - Header-only controller model, rolling windows, state enum, snapshot structs, and pure decision logic. Header-only avoids CMake changes.
- Modify: `src/ImageProcessor.h`
  - Add `noiseSigma` to `CentroidResult`.
  - Add auto-exposure metric configuration and a per-frame auto-exposure sample signal.
- Modify: `src/ImageProcessor.cpp`
  - Populate `noiseSigma`.
  - Emit auto-exposure samples for valid and invalid centroid frames.
  - Count saturated pixels using the configured hard saturation DN.
- Modify: `src/DIMM.h`
  - Replace old peak-sample members with controller state, latest metrics, AE sequence metadata, and helper declarations.
- Modify: `src/DIMM.cpp`
  - Wire settings into controller.
  - Connect the new auto-exposure sample signal.
  - Replace the current 4-hour median `applyAutoExposure()` path with state-machine evaluation.
  - Keep online exposure/template switching continuous.
  - Extend result CSV headers and rows.
- Create: `scripts/verify_auto_exposure_state_machine.py`
  - Standalone verification script for state-machine behavior and static source checks.

## Task 1: Add Verification Harness First

**Files:**
- Create: `scripts/verify_auto_exposure_state_machine.py`

**Interfaces:**
- Consumes: source files under `src/`.
- Produces: one command, `python scripts/verify_auto_exposure_state_machine.py`, that fails before implementation and passes after implementation.

- [ ] **Step 1: Create the verification script**

The script should contain deterministic sequence tests for normal, flicker, sustained bright, sustained dark, star lost, and trend conflict behavior. It should also perform static checks that the implementation does not call stop/restart capture from the auto-exposure path.

Use this structure:

```python
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


@dataclass
class Sample:
    t: int
    peak0: float
    peak1: float
    snr0: float
    snr1: float
    valid0: bool
    valid1: bool
    saturated0: bool = False
    saturated1: bool = False


def assert_source_contains(path: str, needle: str) -> None:
    text = (ROOT / path).read_text(encoding="utf-8")
    if needle not in text:
        raise AssertionError(f"{path} missing {needle}")


def assert_source_not_contains_in_ae_region(path: str, forbidden: str) -> None:
    text = (ROOT / path).read_text(encoding="utf-8")
    marker = "AutoExposure"
    idx = text.find(marker)
    if idx < 0:
        raise AssertionError(f"{path} missing {marker}")
    region = text[idx:]
    if forbidden in region:
        raise AssertionError(f"{path} auto-exposure region contains forbidden call {forbidden}")


def test_static_interfaces() -> None:
    assert_source_contains("src/AutoExposureController.h", "enum class AutoExposureState")
    assert_source_contains("src/AutoExposureController.h", "struct AutoExposureFrameSample")
    assert_source_contains("src/AutoExposureController.h", "class AutoExposureController")
    assert_source_contains("src/ImageProcessor.h", "autoExposureSampleReady")
    assert_source_contains("src/DIMM.cpp", "handleAutoExposureSample")
    assert_source_contains("src/DIMM.cpp", "WEATHER_TOO_DARK")
    assert_source_not_contains_in_ae_region("src/DIMM.cpp", "stopLiveCapture(")
    assert_source_not_contains_in_ae_region("src/DIMM.cpp", "resetRunProcessingState(")


def main() -> None:
    test_static_interfaces()
    print("auto exposure state-machine verification passed")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run the script before implementation**

Run:

```powershell
python scripts/verify_auto_exposure_state_machine.py
```

Expected result before implementation:

```text
AssertionError: src/AutoExposureController.h missing enum class AutoExposureState
```

This verifies the harness catches the missing implementation.

## Task 2: Extend Configuration and Settings UI

**Files:**
- Modify: `src/AppConfig.h`
- Modify: `src/SettingsDialog.h`
- Modify: `src/SettingsDialog.cpp`
- Modify: `src/DIMM.cpp`

**Interfaces:**
- Produces: `AutoExposureConfig` with all state-machine parameters.
- Produces: `SettingsDialog::ConfigCallbacks::applyAutoExposure(const AutoExposureConfig&)`.
- Consumes: existing settings apply flow in `DIMM::setupSettingsDialog()`.

- [ ] **Step 1: Extend `AutoExposureConfig`**

Replace the current compact struct with these fields while preserving existing names where possible:

```cpp
struct AutoExposureConfig {
    bool enabled = false;
    bool useFittedPeak = false;

    double lowThreshold = 80.0;
    double highThreshold = 220.0;
    double darkRatio = 1.2;
    double brightRatio = 0.8;

    double targetPeakLowDn = 3200.0;
    double targetPeakHighDn = 3600.0;
    double nearSaturationDn = 3800.0;
    double hardSaturationDn = 4090.0;
    int saturatedPixelCount = 1;

    double darkSnrWarning = 8.0;
    double darkSnrCritical = 5.0;
    double minValidCentroidRatio = 0.50;
    double starLostValidRatio = 0.10;
    double brightFrameRatioThreshold = 0.30;
    double darkFrameRatioThreshold = 0.50;

    int sampleWindowSec = 60;
    int brightPersistenceSec = 30;
    int darkPersistenceSec = 60;
    int starLostPersistenceSec = 120;
    int trendConflictPersistenceSec = 30;
    int safePersistenceSec = 60;
    int cooldownSec = 180;

    double minExposureUs = 500.0;
    double maxExposureUs = 20000.0;
    int maxTemplateStepPerAdjust = 1;
    double maxExposureChangeRatioUp = 1.30;
    double maxExposureChangeRatioDown = 0.70;
    double cameraAgreementRatio = 0.50;
};
```

- [ ] **Step 2: Change the settings callback signature**

In `src/SettingsDialog.h`, change the callback from a long primitive list to config passing:

```cpp
std::function<void(const AutoExposureConfig& config)> applyAutoExposure;
```

Include `AppConfig.h` if the header does not already see `AutoExposureConfig`.

- [ ] **Step 3: Add UI fields**

Add `QLineEdit*` fields for each numeric parameter and one `QCheckBox*` for `useFittedPeak`. Keep the existing auto-exposure group and add rows with clear Chinese labels:

```cpp
autoExpUseFittedPeakCheck = new QCheckBox(QStringLiteral("优先使用拟合峰值"));
autoExpTargetPeakLowEdit = new QLineEdit(QStringLiteral("3200"));
autoExpTargetPeakHighEdit = new QLineEdit(QStringLiteral("3600"));
autoExpNearSaturationEdit = new QLineEdit(QStringLiteral("3800"));
autoExpHardSaturationEdit = new QLineEdit(QStringLiteral("4090"));
autoExpSaturatedPixelCountEdit = new QLineEdit(QStringLiteral("1"));
autoExpDarkSnrWarningEdit = new QLineEdit(QStringLiteral("8.0"));
autoExpDarkSnrCriticalEdit = new QLineEdit(QStringLiteral("5.0"));
autoExpMinValidCentroidRatioEdit = new QLineEdit(QStringLiteral("0.50"));
autoExpStarLostValidRatioEdit = new QLineEdit(QStringLiteral("0.10"));
autoExpBrightFrameRatioEdit = new QLineEdit(QStringLiteral("0.30"));
autoExpDarkFrameRatioEdit = new QLineEdit(QStringLiteral("0.50"));
autoExpSampleWindowSecEdit = new QLineEdit(QStringLiteral("60"));
autoExpBrightPersistenceSecEdit = new QLineEdit(QStringLiteral("30"));
autoExpDarkPersistenceSecEdit = new QLineEdit(QStringLiteral("60"));
autoExpStarLostPersistenceSecEdit = new QLineEdit(QStringLiteral("120"));
autoExpTrendConflictPersistenceSecEdit = new QLineEdit(QStringLiteral("30"));
autoExpSafePersistenceSecEdit = new QLineEdit(QStringLiteral("60"));
autoExpCooldownSecEdit = new QLineEdit(QStringLiteral("180"));
autoExpMaxTemplateStepEdit = new QLineEdit(QStringLiteral("1"));
autoExpMaxChangeUpEdit = new QLineEdit(QStringLiteral("1.30"));
autoExpMaxChangeDownEdit = new QLineEdit(QStringLiteral("0.70"));
autoExpCameraAgreementRatioEdit = new QLineEdit(QStringLiteral("0.50"));
```

- [ ] **Step 4: Add validation**

Add explicit validation with messages. Required checks:

```text
0 <= targetPeakLowDn < targetPeakHighDn <= nearSaturationDn <= hardSaturationDn <= 4095
saturatedPixelCount >= 1
darkSnrCritical > 0
darkSnrWarning > darkSnrCritical
0 <= starLostValidRatio <= minValidCentroidRatio <= 1
0 <= brightFrameRatioThreshold <= 1
0 <= darkFrameRatioThreshold <= 1
sampleWindowSec >= 10
brightPersistenceSec >= 1
darkPersistenceSec >= 1
starLostPersistenceSec >= darkPersistenceSec
trendConflictPersistenceSec >= 1
safePersistenceSec >= 1
cooldownSec >= 0
minExposureUs > 0
maxExposureUs >= minExposureUs
maxTemplateStepPerAdjust >= 1
maxExposureChangeRatioUp >= 1
0 < maxExposureChangeRatioDown <= 1
cameraAgreementRatio > 0
```

- [ ] **Step 5: Update `DIMM::setupSettingsDialog()`**

Change the lambda to receive the full config:

```cpp
m_settingsDialog->onApplyAutoExposure =
    [this](const AutoExposureConfig& config) {
        m_autoExposureConfig = config;
        resetAutoExposureState();
        if (m_imageProcessor) {
            m_imageProcessor->setAutoExposureMetricConfig(config.hardSaturationDn);
        }
        setStatusMessage(config.enabled
            ? QStringLiteral("自动曝光已启用: 状态机保护模式")
            : QStringLiteral("自动曝光已关闭"),
            UiStatusLevel::Info);
    };
```

`m_autoExposureConfig` is introduced in Task 4.

## Task 3: Emit Auto-Exposure Samples for Every ROI Frame

**Files:**
- Modify: `src/ImageProcessor.h`
- Modify: `src/ImageProcessor.cpp`

**Interfaces:**
- Produces signal:

```cpp
void autoExposureSampleReady(int cameraIndex,
                             double peakValue,
                             double fitPeakValue,
                             double background,
                             double noiseSigma,
                             double threshold,
                             quint64 signalPixelCount,
                             quint64 saturatedPixelCount,
                             bool centroidValid,
                             bool measurementUsable,
                             quint64 frameId,
                             qint64 timestampMs);
```

- Produces slot:

```cpp
void setAutoExposureMetricConfig(double hardSaturationDn);
```

- Consumes `ImageProcessorWorker::processFrame()`.

- [ ] **Step 1: Add `noiseSigma` to `CentroidResult`**

In `src/ImageProcessor.h`:

```cpp
double noiseSigma = 0.0;
```

In `centerOfGravity()`, assign:

```cpp
result.noiseSigma = sigma;
```

In `gaussianFit()`, copy:

```cpp
result.noiseSigma = cog.noiseSigma;
```

- [ ] **Step 2: Add metric config**

In both worker and facade classes, add:

```cpp
void setAutoExposureMetricConfig(double hardSaturationDn);
```

Worker member:

```cpp
double m_autoExposureHardSaturationDn = 4090.0;
```

Facade implementation should queue to the worker:

```cpp
QMetaObject::invokeMethod(m_worker,
                          "setAutoExposureMetricConfig",
                          Qt::QueuedConnection,
                          Q_ARG(double, hardSaturationDn));
```

- [ ] **Step 3: Count saturated pixels**

In `ImageProcessorWorker::processFrame()`, after `roiImage` is available, count raw ROI pixels using `m_autoExposureHardSaturationDn`:

```cpp
quint64 saturatedPixelCount = 0;
const double hardSaturationDn = m_autoExposureHardSaturationDn;
for (int y = 0; y < roiImage.rows; ++y) {
    for (int x = 0; x < roiImage.cols; ++x) {
        if (pixelValueAt(roiImage, y, x) >= hardSaturationDn) {
            ++saturatedPixelCount;
        }
    }
}
```

- [ ] **Step 4: Emit samples before returning from invalid centroid paths**

After `calculateCentroid(correctedRoiImage)`, compute:

```cpp
const bool correctedMeasurementUsable =
    centroid.valid && isMeasurementUsableCentroid(centroid, correctedRoiImage);
const bool rawEdgeSignal =
    centroid.valid && hasThresholdSignalNearRoiEdge(roiImage, centroid.threshold);
const bool measurementUsable = correctedMeasurementUsable && !rawEdgeSignal;
const double fitPeakValue = centroid.peakValue;
```

Emit the new signal once for every non-empty ROI frame:

```cpp
emit autoExposureSampleReady(cameraIndex,
                             centroid.peakValue,
                             fitPeakValue,
                             centroid.background,
                             centroid.noiseSigma,
                             centroid.threshold,
                             centroid.signalPixelCount,
                             saturatedPixelCount,
                             centroid.valid,
                             measurementUsable,
                             frameId,
                             nowMs);
```

Keep the existing `centroidReady` signal behavior unchanged.

- [ ] **Step 5: Verify no frame processing gate was added**

Run:

```powershell
python scripts/verify_auto_exposure_state_machine.py
```

Expected result at this point:

```text
AssertionError: src/AutoExposureController.h missing enum class AutoExposureState
```

The expected failure has moved past the sample signal static check.

## Task 4: Add Header-Only State Machine Controller

**Files:**
- Create: `src/AutoExposureController.h`

**Interfaces:**
- Produces:

```cpp
enum class AutoExposureState;
struct AutoExposureFrameSample;
struct AutoExposureCameraWindowStats;
struct AutoExposureTrendSnapshot;
struct AutoExposureDecision;
class AutoExposureController;
```

- Consumes: `AutoExposureConfig`.

- [ ] **Step 1: Define state and sample structs**

Create `src/AutoExposureController.h`:

```cpp
#pragma once

#include "AppConfig.h"

#include <QDateTime>
#include <QQueue>
#include <QString>
#include <QtGlobal>
#include <QVector>

enum class AutoExposureState {
    Normal,
    BrightWarning,
    BrightAdjusting,
    DarkWarning,
    DarkAdjusting,
    Cooldown,
    StarLost,
    TrendConflict
};

struct AutoExposureFrameSample {
    int cameraIndex = -1;
    double peakDn = 0.0;
    double fitPeakDn = 0.0;
    double backgroundDn = 0.0;
    double noiseSigmaDn = 0.0;
    double thresholdDn = 0.0;
    quint64 signalPixelCount = 0;
    quint64 saturatedPixelCount = 0;
    bool centroidValid = false;
    bool measurementUsable = false;
    quint64 frameId = 0;
    qint64 timestampMs = 0;
};
```

- [ ] **Step 2: Define decision output**

Add:

```cpp
struct AutoExposureCameraWindowStats {
    bool hasSamples = false;
    double peakP50Dn = 0.0;
    double peakP90Dn = 0.0;
    double peakP95Dn = 0.0;
    double medianSnr = 0.0;
    double validCentroidRatio = 0.0;
    double measurementUsableRatio = 0.0;
    double saturationFrameRatio = 0.0;
    double darkFrameRatio = 0.0;
};

struct AutoExposureTrendSnapshot {
    AutoExposureCameraWindowStats camera[2];
    bool commonTrendValid = false;
    bool trendConflict = false;
    double sharedPeakP50Dn = 0.0;
    double sharedPeakP90Dn = 0.0;
    double sharedPeakP95Dn = 0.0;
    double sharedSnr = 0.0;
    double sharedValidRatio = 0.0;
    double sharedSaturationRatio = 0.0;
    double sharedDarkRatio = 0.0;
};

struct AutoExposureDecision {
    AutoExposureState state = AutoExposureState::Normal;
    bool shouldAdjustExposure = false;
    int targetExposureUs = 0;
    QString reason;
    AutoExposureTrendSnapshot snapshot;
};
```

- [ ] **Step 3: Implement rolling window helpers**

Implement methods in the header:

```cpp
class AutoExposureController {
public:
    void configure(const AutoExposureConfig& config);
    void reset();
    AutoExposureDecision addSampleAndEvaluate(const AutoExposureFrameSample& sample,
                                              int currentExposureUs,
                                              const QVector<int>& templateExposures,
                                              qint64 nowMs);
    AutoExposureState state() const { return m_state; }
    AutoExposureTrendSnapshot latestSnapshot() const { return m_latestSnapshot; }
    QString latestReason() const { return m_latestReason; }

private:
    AutoExposureConfig m_config;
    AutoExposureState m_state = AutoExposureState::Normal;
    QQueue<AutoExposureFrameSample> m_samples[2];
    AutoExposureTrendSnapshot m_latestSnapshot;
    QString m_latestReason;
    qint64 m_brightSinceMs = -1;
    qint64 m_darkSinceMs = -1;
    qint64 m_conflictSinceMs = -1;
    qint64 m_starLostSinceMs = -1;
    qint64 m_safeSinceMs = -1;
    qint64 m_cooldownUntilMs = -1;

    void prune(qint64 nowMs);
    AutoExposureCameraWindowStats statsForCamera(int cameraIndex) const;
    AutoExposureTrendSnapshot buildSnapshot() const;
    int chooseTargetExposure(int currentExposureUs,
                             const QVector<int>& templateExposures,
                             const AutoExposureTrendSnapshot& snapshot,
                             bool brighten) const;
};
```

- [ ] **Step 4: Implement state transitions**

Transition rules:

```text
trend conflict -> TrendConflict, no exposure change
cooldown active -> Cooldown, no exposure change
star lost at max exposure -> StarLost, no exposure change
sustained bright -> BrightAdjusting, shouldAdjustExposure=true
sustained dark and not max exposure -> DarkAdjusting, shouldAdjustExposure=true
bright condition present -> BrightWarning
dark condition present -> DarkWarning
safe condition sustained -> Normal
```

Use milliseconds derived from config seconds:

```cpp
const qint64 brightPersistenceMs = qint64(m_config.brightPersistenceSec) * 1000;
const qint64 darkPersistenceMs = qint64(m_config.darkPersistenceSec) * 1000;
const qint64 cooldownMs = qint64(m_config.cooldownSec) * 1000;
```

- [ ] **Step 5: Implement target exposure selection**

Rules:

```text
brighten target raw = currentExposure * clamp(targetPeakLowDn / max(sharedPeakP50Dn, 1), 1, maxExposureChangeRatioUp)
darken target raw = currentExposure * clamp(targetPeakHighDn / max(sharedPeakP90Dn, 1), maxExposureChangeRatioDown, 1)
clamp target raw to minExposureUs..maxExposureUs
map target raw to nearest allowed hot-pixel template exposure
limit template index movement to maxTemplateStepPerAdjust
```

If the template list is empty, return `currentExposureUs` and reason `自动曝光: 没有可用热像素模板，保持当前曝光`.

## Task 5: Wire Controller into DIMM

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.cpp`

**Interfaces:**
- Consumes `AutoExposureController`.
- Produces:

```cpp
void handleAutoExposureSample(const AutoExposureFrameSample& sample);
void resetAutoExposureState();
QString autoExposureStateName(AutoExposureState state) const;
QString csvSafeField(QString value) const;
```

- [ ] **Step 1: Replace old members**

In `src/DIMM.h`, remove or stop using these old members:

```cpp
double m_autoExposureLowThreshold;
double m_autoExposureHighThreshold;
double m_autoExposureDarkRatio;
double m_autoExposureBrightRatio;
double m_autoExposureMinUs;
double m_autoExposureMaxUs;
int m_autoExposureIntervalMs;
QVector<double> m_autoExposurePeakSamples[2];
qint64 m_lastAutoExposureCheckMs;
```

Add:

```cpp
AutoExposureConfig m_autoExposureConfig;
AutoExposureController m_autoExposureController;
AutoExposureTrendSnapshot m_latestAutoExposureTrend;
AutoExposureState m_autoExposureState = AutoExposureState::Normal;
QString m_autoExposureReason;
quint64 m_autoExposureSequenceId = 0;
int m_autoExposureTargetExposureUs = 0;
qint64 m_lastAutoExposureAdjustMs = -1;
quint64 m_autoExposureFramesSinceAdjust = 0;
double m_latestAutoExposurePeakDn[2] = {0.0, 0.0};
double m_latestAutoExposureSnr[2] = {0.0, 0.0};
double m_latestAutoExposureValidRatio[2] = {0.0, 0.0};
```

- [ ] **Step 2: Connect the sample signal**

In `DIMM::DIMM()` or the existing setup area where `centroidReady` is connected, add:

```cpp
connect(m_imageProcessor,
        &ImageProcessor::autoExposureSampleReady,
        this,
        [this](int cameraIndex,
               double peakValue,
               double fitPeakValue,
               double background,
               double noiseSigma,
               double threshold,
               quint64 signalPixelCount,
               quint64 saturatedPixelCount,
               bool centroidValid,
               bool measurementUsable,
               quint64 frameId,
               qint64 timestampMs) {
            AutoExposureFrameSample sample;
            sample.cameraIndex = cameraIndex;
            sample.peakDn = peakValue;
            sample.fitPeakDn = fitPeakValue;
            sample.backgroundDn = background;
            sample.noiseSigmaDn = noiseSigma;
            sample.thresholdDn = threshold;
            sample.signalPixelCount = signalPixelCount;
            sample.saturatedPixelCount = saturatedPixelCount;
            sample.centroidValid = centroidValid;
            sample.measurementUsable = measurementUsable;
            sample.frameId = frameId;
            sample.timestampMs = timestampMs;
            handleAutoExposureSample(sample);
        });
```

- [ ] **Step 3: Stop calling old `applyAutoExposure()` from `centroidReady`**

Remove the call:

```cpp
applyAutoExposure(camIdx, peakValue);
```

Do not change the rest of the centroid path. The `centroidReady` lambda must still update UI/runtime centroids exactly as before.

- [ ] **Step 4: Implement `handleAutoExposureSample()`**

Logic:

```cpp
if (!m_autoExposureConfig.enabled) return;
if (m_captureState != CaptureState::Live) return;
if (m_liveStartupPhase != LiveStartupPhase::Tracking) return;
if (sample.cameraIndex < 0 || sample.cameraIndex >= 2) return;

const QVector<int> templates = scanHotPixelExposureTemplates();
const int currentExposure = qRound(m_configExposureUs);
AutoExposureDecision decision =
    m_autoExposureController.addSampleAndEvaluate(sample, currentExposure, templates, sample.timestampMs);

m_autoExposureState = decision.state;
m_autoExposureReason = decision.reason;
m_latestAutoExposureTrend = decision.snapshot;
updateLatestAutoExposureDiagnostics(decision.snapshot);

if (m_autoExposureFramesSinceAdjust < std::numeric_limits<quint64>::max()) {
    ++m_autoExposureFramesSinceAdjust;
}

if (decision.shouldAdjustExposure && decision.targetExposureUs > 0) {
    QString reason;
    const bool applied = applyExposureAndHotPixelTemplate(decision.targetExposureUs, &reason);
    if (applied) {
        ++m_autoExposureSequenceId;
        m_autoExposureTargetExposureUs = decision.targetExposureUs;
        m_lastAutoExposureAdjustMs = sample.timestampMs;
        m_autoExposureFramesSinceAdjust = 0;
    } else {
        m_autoExposureReason = reason;
    }
}
```

Use `<limits>` if it is not already included.

- [ ] **Step 5: Keep `applyAutoExposure()` only as a removed or private-deprecated path**

Preferred result: remove `applyAutoExposure()` and `selectTemplateExposureForPeak()` if no other code uses them. If removing creates churn, leave a small private wrapper unused by the new signal and document in the final report that the new controller path supersedes it.

## Task 6: Keep Online Exposure and Template Switching Continuous

**Files:**
- Modify: `src/DIMM.cpp`

**Interfaces:**
- Consumes existing `scanHotPixelExposureTemplates()`.
- Consumes existing `resolveHotPixelTemplatePathsForExposure()`.
- Consumes existing `CameraManager::setExposure()`.
- Consumes existing `ImageProcessor::configureHotPixelTemplates()`.

- [ ] **Step 1: Pre-resolve template paths before changing exposure**

At the top of `applyExposureAndHotPixelTemplate(int exposureUs, QString* reason)`, keep the existing path resolution and failure behavior. If target templates are missing, return `false` and keep the old exposure.

- [ ] **Step 2: Apply same exposure to both cameras**

Keep the current loop over cameras:

```cpp
for (int i = 0; i < 2; ++i) {
    if (m_cameraManager->isCameraOpen(i) &&
        !m_cameraManager->setExposure(i, exposureUs)) {
        // return false with reason
    }
}
```

Do not call `stopLiveCapture()`, `startLiveCapture()`, `advanceAcquisitionGeneration()`, or `resetRunProcessingState()` here.

- [ ] **Step 3: Reconfigure hot-pixel template immediately after exposure write**

Use the already resolved paths:

```cpp
m_imageProcessor->configureHotPixelTemplates(PathUtils::resolvePathFromAppDir(m_hotPixelCamera0MaskPath),
                                             PathUtils::resolvePathFromAppDir(m_hotPixelCamera0ExcessPath),
                                             PathUtils::resolvePathFromAppDir(m_hotPixelCamera1MaskPath),
                                             PathUtils::resolvePathFromAppDir(m_hotPixelCamera1ExcessPath),
                                             m_hotPixelTemplateWidth,
                                             m_hotPixelTemplateHeight);
```

This is an online switch. It may produce a few frames near the transition whose physical exposure and template are not perfectly aligned. Those frames must remain in computation and saving, and the new CSV metadata from Task 7 must identify the transition.

- [ ] **Step 4: Report state without interrupting acquisition**

Use status text only:

```cpp
setStatusMessage(QStringLiteral("自动曝光: %1 -> %2 μs，状态 %3")
                     .arg(oldExposure)
                     .arg(exposureUs)
                     .arg(autoExposureStateName(m_autoExposureState)),
                 UiStatusLevel::Warning);
```

## Task 7: Extend Result CSV and Runtime Diagnostics

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.cpp`

**Interfaces:**
- Consumes latest AE trend and sequence members.
- Produces CSV fields:

```text
camera1_peak_dn
camera2_peak_dn
camera1_snr
camera2_snr
camera1_valid_ratio
camera2_valid_ratio
exposure_us
hot_pixel_template_exposure_us
ae_enabled
ae_state
ae_reason
ae_sequence_id
ae_target_exposure_us
ae_frames_since_adjust
```

- [ ] **Step 1: Update CSV header**

In `initResultFile()`, append the new column names after existing frame-rate or ROI diagnostic fields and before final atmospheric values. Keep old columns in the same order.

- [ ] **Step 2: Add CSV-safe helper**

In `DIMM.cpp`:

```cpp
QString DIMM::csvSafeField(QString value) const
{
    value.replace(QLatin1Char(','), QLatin1Char(';'));
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return value;
}
```

- [ ] **Step 3: Append row values**

In `saveResultRow(int frame)`, append:

```cpp
QString::number(m_latestAutoExposurePeakDn[0], 'f', 1)
QString::number(m_latestAutoExposurePeakDn[1], 'f', 1)
QString::number(m_latestAutoExposureSnr[0], 'f', 2)
QString::number(m_latestAutoExposureSnr[1], 'f', 2)
QString::number(m_latestAutoExposureValidRatio[0], 'f', 3)
QString::number(m_latestAutoExposureValidRatio[1], 'f', 3)
QString::number(m_configExposureUs, 'f', 0)
QString::number(m_hotPixelTemplateExposureUs)
m_autoExposureConfig.enabled ? QStringLiteral("1") : QStringLiteral("0")
autoExposureStateName(m_autoExposureState)
csvSafeField(m_autoExposureReason)
QString::number(m_autoExposureSequenceId)
QString::number(m_autoExposureTargetExposureUs)
QString::number(m_autoExposureFramesSinceAdjust)
```

- [ ] **Step 4: Update status labels**

When state changes to `STAR_LOST`, call:

```cpp
setStatusMessage(QStringLiteral("自动曝光: WEATHER_TOO_DARK / STAR_LOST，最大曝光下仍无法稳定观测星点"),
                 UiStatusLevel::Error);
```

When state changes to `TREND_CONFLICT`, call:

```cpp
setStatusMessage(QStringLiteral("自动曝光: 两台相机亮度趋势冲突，保持当前曝光"),
                 UiStatusLevel::Warning);
```

Throttle repeated messages so the same state does not spam the UI every frame.

## Task 8: Verification

**Files:**
- Modify: `scripts/verify_auto_exposure_state_machine.py` if static checks need exact final names.

**Interfaces:**
- Consumes all implementation tasks.
- Produces a final verification command and optional build attempt.

- [ ] **Step 1: Run standalone verification**

Run:

```powershell
python scripts/verify_auto_exposure_state_machine.py
```

Expected:

```text
auto exposure state-machine verification passed
```

- [ ] **Step 2: Run existing SDK-style ROI verification**

Run:

```powershell
python scripts/verify_sdk_style_star_roi.py
```

Expected:

```text
SDK-style star/ROI verification passed
```

- [ ] **Step 3: Try existing build without reconfiguring CMake**

Run only if the local build environment is usable:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build 'D:\Daheng Imaging\build-dimm' --config Release --target DIMM
```

Expected if the environment is healthy:

```text
Build succeeded
```

If the local MSBuild environment still fails with the known `Path` / `PATH` duplicate-key problem, record that exact failure and do not modify CMake.

## Self-Review

Spec coverage:

- Continuous acquisition is covered by Tasks 5 and 6.
- Keeping frames in `r0/seeing` and result saving is covered by Tasks 5, 6, and 7.
- Two-camera common trend is covered by Task 4.
- Overexposure protection is covered by Task 4.
- Over-dark and `STAR_LOST` reporting are covered by Tasks 4 and 7.
- Adjustable parameters are covered by Task 2.
- Online hot-pixel template switching is covered by Task 6.
- CSV diagnostics are covered by Task 7.
- Non-use of SDK auto exposure is covered by Global Constraints.

Placeholder scan:

- The plan avoids `TBD`, empty implementation steps, and references to undefined task outputs.

Type consistency:

- `AutoExposureConfig` is defined in Task 2 and consumed by Tasks 4 and 5.
- `AutoExposureFrameSample`, `AutoExposureDecision`, and `AutoExposureController` are defined in Task 4 and consumed by Task 5.
- `autoExposureSampleReady` is defined in Task 3 and consumed by Task 5.
- `csvSafeField` is declared in Task 5 and implemented in Task 7.

