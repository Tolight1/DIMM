# One Minute Atmosphere Window Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Change DIMM atmospheric parameter calculation from a 1-second sample window to a 60-second rolling sample window, with no valid atmospheric parameters emitted during the first minute and 1 Hz parameter updates after the first full minute.

**Architecture:** Keep the existing acquisition, trigger, centroid extraction, and frame pairing flow unchanged. Only enlarge the differential sample history used by `calculateAtmosphere()`, keep the 1 Hz publish gate, and split the short pending-pair queue limit away from the long atmosphere history window so unpaired queues do not grow to 60 seconds. Existing UI behavior already shows `--` while `hasValidAtmosphere == false`; after one minute the existing `atmosphereReady` signal continues to refresh `r0 / seeing / theta0 / tau0` once per second.

**Tech Stack:** Qt 6 C++17, OpenCV, Python `unittest` static regression tests. Do not run a CMake/MSBuild build in this task; the user will build manually.

---

## Non-Negotiable Constraints

- Do not modify camera acquisition, hardware trigger, frame ID alignment, timestamp offset calibration, ROI tracking, centroid extraction, or `appendDifferentialSample()` pairing semantics.
- Do not change `ATMOSPHERE_PUBLISH_INTERVAL_MS = 1000`; updates should still be at most once per second after the 60-second window is full.
- Do not save or load new settings for the window length. Use a fixed 60-second constant for this task.
- Do not run CMake/MSBuild build commands. Verification for this plan is Python static tests only.
- Keep `D = 56 mm`, baseline default `250 mm`, focal default `26.9 cm`, zenith default `49.6 deg`, and baseline angle behavior unchanged.
- Preserve the current document-formula changes already made for `r0 / seeing / theta0 / tau0`.

## Current Code Facts To Use

- `src/ImageProcessor.h` currently has:

```cpp
static constexpr int MIN_HISTORY_WINDOW = 50;
static constexpr int MAX_HISTORY_WINDOW = 1000;
static constexpr qint64 ATMOSPHERE_PUBLISH_INTERVAL_MS = 1000;
```

- `src/ImageProcessor.cpp` currently has:

```cpp
int ImageProcessorWorker::historyWindowSize() const
{
    QMutexLocker locker(&m_mutex);
    return std::clamp(static_cast<int>(std::lround(m_targetFrameRateHz)),
                      MIN_HISTORY_WINDOW,
                      MAX_HISTORY_WINDOW);
}

int ImageProcessorWorker::minimumAtmosphereSamples() const
{
    return historyWindowSize();
}
```

- `appendDifferentialSample()` appends to `m_differentialHistory` and trims with `historyWindowSize()`.
- `processFrame()` checks `m_differentialHistory.size() < minimumAtmosphereSamples()` before calling `calculateAtmosphere()`.
- `processFrame()` also trims `m_pendingCentroids[cameraIndex]` using `historyWindowSize()`. This must be changed to a new short queue limit so pending unpaired samples stay bounded near the old 1-second behavior.
- `DIMM::setupAtmosphereProcessorConnection()` only calls `saveResultRow()` when `atmosphereReady` is emitted. Therefore main result rows start after the first valid 60-second atmosphere calculation.

## Files

- Modify: `src/ImageProcessor.h`
- Modify: `src/ImageProcessor.cpp`
- Create: `tests/test_one_minute_atmosphere_window_static.py`
- Do not modify: `src/CameraManager.*`, hardware trigger code, frame-pairing comparisons, ROI localization, centroid algorithms, `SettingsDialog.*`, `DIMM.ui`

---

### Task 1: Add Failing Static Tests For The 60-Second Window

**Files:**
- Create: `tests/test_one_minute_atmosphere_window_static.py`

- [ ] **Step 1: Create the test file**

Create `tests/test_one_minute_atmosphere_window_static.py` with exactly this content:

```python
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class OneMinuteAtmosphereWindowStaticTest(unittest.TestCase):
    def test_header_declares_separate_atmosphere_and_pair_queue_limits(self):
        header = read("src/ImageProcessor.h")

        self.assertIn("static constexpr double ATMOSPHERE_HISTORY_WINDOW_SECONDS = 60.0", header)
        self.assertIn("static constexpr int MAX_HISTORY_WINDOW = 60000", header)
        self.assertIn("static constexpr int MAX_PENDING_PAIR_QUEUE = 1000", header)
        self.assertIn("int pendingCentroidQueueLimit() const", header)
        self.assertIn("int historyWindowSize() const", header)
        self.assertIn("int minimumAtmosphereSamples() const", header)

    def test_history_window_is_sixty_seconds_but_publish_interval_stays_one_second(self):
        source = read("src/ImageProcessor.cpp")
        history_body = source.split("int ImageProcessorWorker::historyWindowSize() const", 1)[1].split(
            "int ImageProcessorWorker::minimumAtmosphereSamples() const", 1
        )[0]
        minimum_body = source.split("int ImageProcessorWorker::minimumAtmosphereSamples() const", 1)[1].split(
            "int ImageProcessorWorker::pendingCentroidQueueLimit() const", 1
        )[0]

        self.assertIn("m_targetFrameRateHz * ATMOSPHERE_HISTORY_WINDOW_SECONDS", history_body)
        self.assertIn("MIN_HISTORY_WINDOW", history_body)
        self.assertIn("MAX_HISTORY_WINDOW", history_body)
        self.assertIn("return historyWindowSize()", minimum_body)
        self.assertIn("ATMOSPHERE_PUBLISH_INTERVAL_MS = 1000", read("src/ImageProcessor.h"))

    def test_pending_pair_queue_keeps_short_limit(self):
        source = read("src/ImageProcessor.cpp")
        pending_body = source.split("int ImageProcessorWorker::pendingCentroidQueueLimit() const", 1)[1].split(
            "void ImageProcessorWorker::resetRoiProcessingHistory()", 1
        )[0]
        process_body = source.split("void ImageProcessorWorker::processFrame", 1)[1].split(
            "ImageProcessor::ImageProcessor", 1
        )[0]

        self.assertIn("std::lround(m_targetFrameRateHz)", pending_body)
        self.assertIn("MAX_PENDING_PAIR_QUEUE", pending_body)
        self.assertIn("while (m_pendingCentroids[cameraIndex].size() > pendingCentroidQueueLimit())", process_body)
        self.assertNotIn("while (m_pendingCentroids[cameraIndex].size() > historyWindowSize())", process_body)

    def test_atmosphere_not_calculated_until_full_minute_history_exists(self):
        source = read("src/ImageProcessor.cpp")
        process_body = source.split("void ImageProcessorWorker::processFrame", 1)[1].split(
            "ImageProcessor::ImageProcessor", 1
        )[0]
        append_body = source.split("bool ImageProcessorWorker::appendDifferentialSample()", 1)[1].split(
            "void ImageProcessorWorker::emitRoiImageIfDue", 1
        )[0]

        self.assertIn("while (m_differentialHistory.size() > historyWindowSize())", append_body)
        self.assertIn("m_differentialHistory.size() < minimumAtmosphereSamples()", process_body)
        self.assertLess(
            process_body.find("m_differentialHistory.size() < minimumAtmosphereSamples()"),
            process_body.find("calculateAtmosphere(m_differentialHistory)"),
        )
        self.assertLess(
            process_body.find("m_lastAtmospherePublishMs > 0"),
            process_body.find("calculateAtmosphere(m_differentialHistory)"),
        )


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the new test and verify it fails**

Run:

```powershell
python -m unittest tests.test_one_minute_atmosphere_window_static
```

Expected result before implementation:

```text
FAILED
```

The failures should mention missing `ATMOSPHERE_HISTORY_WINDOW_SECONDS`, missing `MAX_PENDING_PAIR_QUEUE`, or pending queue still using `historyWindowSize()`.

---

### Task 2: Add Separate Constants And Method Declaration

**Files:**
- Modify: `src/ImageProcessor.h`

- [ ] **Step 1: Update constants in `ImageProcessorWorker`**

Find this block in `src/ImageProcessor.h`:

```cpp
static constexpr int MIN_HISTORY_WINDOW = 50;
static constexpr int MAX_HISTORY_WINDOW = 1000;
static constexpr int MIN_ROI_SIZE = 16;
static constexpr qint64 ATMOSPHERE_PUBLISH_INTERVAL_MS = 1000;
```

Replace it with:

```cpp
static constexpr double ATMOSPHERE_HISTORY_WINDOW_SECONDS = 60.0;
static constexpr int MIN_HISTORY_WINDOW = 50;
static constexpr int MAX_HISTORY_WINDOW = 60000;
static constexpr int MAX_PENDING_PAIR_QUEUE = 1000;
static constexpr int MIN_ROI_SIZE = 16;
static constexpr qint64 ATMOSPHERE_PUBLISH_INTERVAL_MS = 1000;
```

Why:
- `MAX_HISTORY_WINDOW = 60000` covers `60 s * 1000 Hz`, matching the existing `setTargetFrameRateHz()` clamp.
- `MAX_PENDING_PAIR_QUEUE = 1000` preserves the old short protection behavior for unpaired queues.

- [ ] **Step 2: Declare the pending queue helper**

Find this section in `src/ImageProcessor.h`:

```cpp
int historyWindowSize() const;
int minimumAtmosphereSamples() const;
void resetRoiProcessingHistory();
```

Replace it with:

```cpp
int historyWindowSize() const;
int minimumAtmosphereSamples() const;
int pendingCentroidQueueLimit() const;
void resetRoiProcessingHistory();
```

- [ ] **Step 3: Run the new test and confirm it still fails**

Run:

```powershell
python -m unittest tests.test_one_minute_atmosphere_window_static
```

Expected:

```text
FAILED
```

Remaining failures should be in `src/ImageProcessor.cpp` implementation.

---

### Task 3: Change The Atmosphere History Window To 60 Seconds

**Files:**
- Modify: `src/ImageProcessor.cpp`

- [ ] **Step 1: Replace `historyWindowSize()` and `minimumAtmosphereSamples()`**

Find:

```cpp
int ImageProcessorWorker::historyWindowSize() const
{
    QMutexLocker locker(&m_mutex);
    return std::clamp(static_cast<int>(std::lround(m_targetFrameRateHz)),
                      MIN_HISTORY_WINDOW,
                      MAX_HISTORY_WINDOW);
}

int ImageProcessorWorker::minimumAtmosphereSamples() const
{
    return historyWindowSize();
}
```

Replace with:

```cpp
int ImageProcessorWorker::historyWindowSize() const
{
    QMutexLocker locker(&m_mutex);
    return std::clamp(
        static_cast<int>(std::lround(m_targetFrameRateHz * ATMOSPHERE_HISTORY_WINDOW_SECONDS)),
        MIN_HISTORY_WINDOW,
        MAX_HISTORY_WINDOW);
}

int ImageProcessorWorker::minimumAtmosphereSamples() const
{
    return historyWindowSize();
}

int ImageProcessorWorker::pendingCentroidQueueLimit() const
{
    QMutexLocker locker(&m_mutex);
    return std::clamp(static_cast<int>(std::lround(m_targetFrameRateHz)),
                      MIN_HISTORY_WINDOW,
                      MAX_PENDING_PAIR_QUEUE);
}
```

Expected runtime behavior:
- At 200 Hz, `historyWindowSize()` returns `12000`.
- At 1000 Hz, `historyWindowSize()` returns `60000`.
- `minimumAtmosphereSamples()` requires the full 60-second window before any atmospheric parameter is emitted.
- `ATMOSPHERE_PUBLISH_INTERVAL_MS` remains `1000`, so after the first minute the UI updates once per second.

- [ ] **Step 2: Keep differential history trimming on the long window**

Confirm `appendDifferentialSample()` still contains:

```cpp
m_differentialHistory.append(sample);
while (m_differentialHistory.size() > historyWindowSize()) {
    m_differentialHistory.removeFirst();
}
```

Do not change this block.

- [ ] **Step 3: Run the new test and confirm one failure remains**

Run:

```powershell
python -m unittest tests.test_one_minute_atmosphere_window_static
```

Expected:

```text
FAILED
```

The remaining failure should be that pending centroid queues still use `historyWindowSize()`.

---

### Task 4: Keep Pending Pair Queues Short

**Files:**
- Modify: `src/ImageProcessor.cpp`

- [ ] **Step 1: Replace the pending centroid queue limit**

Find this code in `ImageProcessorWorker::processFrame()`:

```cpp
m_pendingCentroids[cameraIndex].append(pending);
while (m_pendingCentroids[cameraIndex].size() > historyWindowSize()) {
    m_pendingCentroids[cameraIndex].removeFirst();
    ++m_droppedUnpairedSamples;
}
```

Replace with:

```cpp
m_pendingCentroids[cameraIndex].append(pending);
while (m_pendingCentroids[cameraIndex].size() > pendingCentroidQueueLimit()) {
    m_pendingCentroids[cameraIndex].removeFirst();
    ++m_droppedUnpairedSamples;
}
```

Why:
- The atmosphere calculation needs 60 seconds of paired differential samples.
- The pending pair queues are only a temporary alignment buffer.
- Letting pending queues grow to 60 seconds would hide synchronization problems and increase memory use if one camera stops producing usable centroids.

- [ ] **Step 2: Run the new test and verify it passes**

Run:

```powershell
python -m unittest tests.test_one_minute_atmosphere_window_static
```

Expected:

```text
OK
```

---

### Task 5: Verify Existing Parameter Formula And Save Tests Still Pass

**Files:**
- No source changes in this task.

- [ ] **Step 1: Run focused regression tests**

Run:

```powershell
python -m unittest tests.test_one_minute_atmosphere_window_static tests.test_atmosphere_formula_and_ui_static tests.test_parameter_validation_export_static tests.test_high_rate_ui_throttle_static tests.test_dynamic_frame_rate_static tests.test_roi_update_preserves_run_counters_static
```

Expected:

```text
OK
```

If `tests.test_dynamic_frame_rate_static` fails because it reads `src/DIMM.Ui.cpp`, inspect the current repository before changing it. In this workspace the active implementation is in `src/DIMM.cpp`; do not create or edit `src/DIMM.Ui.cpp` just to satisfy an obsolete test.

- [ ] **Step 2: Run a symbol scan**

Run:

```powershell
rg -n "ATMOSPHERE_HISTORY_WINDOW_SECONDS|MAX_HISTORY_WINDOW|MAX_PENDING_PAIR_QUEUE|pendingCentroidQueueLimit|historyWindowSize\\(\\)|minimumAtmosphereSamples\\(\\)|ATMOSPHERE_PUBLISH_INTERVAL_MS" src\\ImageProcessor.h src\\ImageProcessor.cpp
```

Expected findings:
- `ATMOSPHERE_HISTORY_WINDOW_SECONDS = 60.0` exists in `src/ImageProcessor.h`.
- `MAX_HISTORY_WINDOW = 60000` exists in `src/ImageProcessor.h`.
- `MAX_PENDING_PAIR_QUEUE = 1000` exists in `src/ImageProcessor.h`.
- `historyWindowSize()` multiplies `m_targetFrameRateHz * ATMOSPHERE_HISTORY_WINDOW_SECONDS`.
- `pendingCentroidQueueLimit()` uses `std::lround(m_targetFrameRateHz)` and `MAX_PENDING_PAIR_QUEUE`.
- `ATMOSPHERE_PUBLISH_INTERVAL_MS = 1000` is unchanged.

---

### Task 6: Manual Behavior Checklist For The User's Build

**Files:**
- No source changes in this task.

- [ ] **Step 1: Explain expected runtime behavior to the user**

Report these exact expectations:

```text
At 200 Hz, the first valid atmospheric parameter update appears after about 12000 paired samples.
Before that, r0 / seeing / theta0 / tau0 stay invalid in the runtime and the UI keeps showing --.
After the first minute, the calculation uses the most recent 60 seconds of paired differential samples and updates at most once per second.
The main CSV column variance_sample_count should be near 12000 at 200 Hz, not near 200.
The pairing and dropped-unpaired counters keep their existing behavior because pendingCentroidQueueLimit() stays around one second of samples.
```

- [ ] **Step 2: Do not run a build**

Do not run any command matching these patterns:

```powershell
cmake --build
msbuild
ninja
```

The user explicitly said they will build manually.

---

## Notes For The Implementing Agent

- The UI update rate is controlled by the existing `ATMOSPHERE_PUBLISH_INTERVAL_MS = 1000` gate in `ImageProcessorWorker::processFrame()`. Do not add a new UI timer.
- The one-minute startup delay comes from `m_differentialHistory.size() < minimumAtmosphereSamples()`. Do not add UI-specific sleeps or timers.
- `calculateAtmosphere(m_differentialHistory)` already computes variance over every sample in the list. Once `m_differentialHistory` holds 60 seconds, the formula automatically uses the full minute.
- `variance_sample_count` in the saved main result is already emitted from `params.sampleCount`. No CSV data column needs to be added for this task.
- If parameter validation mode is enabled, detailed paired centroids are still collected from `differentialSampleDetailReady`. This plan does not change detail-file save cadence.
- If performance becomes a problem after the 60-second window, profile `estimateScalarAutocorrelationCrossingMs()` in a separate task. Do not mix autocorrelation optimization into this window-size change unless the user's manual build/run shows a measurable issue.

## Completion Criteria

- The new static test file exists and passes.
- Focused Python tests listed in Task 5 pass, except for tests already known to be stale before this task.
- No camera acquisition, trigger, ROI, centroid, or frame-pairing logic was changed.
- No CMake/MSBuild build was run by the implementing agent.
- The final response mentions that the user should expect no atmospheric parameter values for the first minute of capture.
