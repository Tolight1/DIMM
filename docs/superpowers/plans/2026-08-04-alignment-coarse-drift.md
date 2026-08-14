# Alignment Coarse Drift Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a coarse alignment state inside alignment mode that estimates the North Celestial Pole (NCP) position from full-frame star drift, without running `PolarisSolver` catalog matching.

**Architecture:** Keep `CaptureState::Alignment` as the outer camera mode. Add an inner coarse-drift state that submits low-rate Mono12 full-frame frames to a dedicated background controller. The controller detects full-frame star candidates using the current full-frame star-detection settings, tracks centroids across frames, fits per-star velocity, solves the NCP center by least squares, and returns a lightweight overlay result to the UI.

**Tech Stack:** Qt6 C++17, OpenCV, existing `CameraManager` / `FullFrameCanvas` / `FullFrameStarDetector`, Python `unittest` static tests only. Do not run a full CMake/MSBuild build unless the user explicitly asks.

---

## Hard Rules

- Do not build. The user explicitly said they will build locally.
- Do not run CMake or MSBuild.
- Use `apply_patch` for manual edits.
- Do not rewrite large C++ files through PowerShell `Set-Content`; this repo has Chinese UI text and encoding damage is easy.
- Keep the existing automatic Polaris solver behavior unchanged when coarse drift is inactive.
- Coarse drift must not call `PolarisSolver`, `PolarisSolverController`, `solveFrame`, or `solveDetectedStars`.
- Coarse drift must use Mono12/raw grayscale processing. Do not normalize the image before thresholding/detection.
- Full-frame star-detection settings must affect coarse drift immediately on the next submitted coarse frame.
- Heavy full-frame detection must run off the UI thread.
- Limit candidates and queued work so a dense star field or overly loose threshold cannot stall the UI.

Suggested post-edit replacement-character check:

```powershell
@'
from pathlib import Path
for rel in [
    "src/DIMM.h",
    "src/DIMM.cpp",
    "src/DIMM.Ui.cpp",
    "src/DIMM.Alignment.cpp",
    "src/CanvasWidgets.h",
    "src/CanvasWidgets.cpp",
]:
    text = Path(rel).read_text(encoding="utf-8-sig")
    count = text.count(chr(0xfffd))
    print(f"{rel}: replacement-char count = {count}")
    if count:
        raise SystemExit(1)
'@ | python -
```

Expected: every count is `0`.

---

## Current Code Context

- Alignment mode lives in `src/DIMM.Alignment.cpp`.
- Runtime UI actions are created in `src/DIMM.cpp::setupRuntimeActions`.
- Embedded preview buttons are created in `src/DIMM.Ui.cpp::setupFullFramePreviewCanvases`.
- Button/action enabled states are updated in `src/DIMM.Ui.cpp::refreshActionStates`.
- Full-frame drawing is in `src/CanvasWidgets.h/.cpp`.
- Current candidate detection is in `src/FullFrameStarDetector.h/.cpp`.
- Runtime full-frame star-detection settings are in `src/InitialStarDetectionConfig.h/.cpp`.
- Existing automatic Polaris solve configuration is built in `src/DIMM.Alignment.cpp::buildPolarisSolverConfig`.
- Automatic solver uses a local NCP-plane catalog, not an all-sky solver. Do not reuse it for rough pointing.

The default optical scale is:

```text
plateScaleArcsecPx = 206265 * 0.0025 / 269 = about 1.917 arcsec/px
full frame width = 5120 * 1.917 / 3600 = about 2.73 deg
Polaris orbit radius = 37.6 * 60 / 1.917 = about 1177 px
```

---

## File Structure

Create:

- `src/AlignmentCoarseEstimator.h`
  - Pure data structs and the stateful centroid tracker / NCP estimator.
  - No Qt widgets.
  - No `PolarisSolver` include.

- `src/AlignmentCoarseEstimator.cpp`
  - Candidate limiting, centroid association, velocity linear regression, NCP least-squares solve, quality status generation.

- `src/AlignmentCoarseController.h`
  - QObject/QThread wrapper for background frame processing.
  - Public `submitFrame`, `resetCamera`, `resetAll`, `cancelAll`.

- `src/AlignmentCoarseController.cpp`
  - Single worker thread, latest-frame-per-camera queue, explicit `InitialStarDetectionConfig` passed into detection, emits `estimateReady`.

Modify:

- `src/FullFrameStarDetector.h/.cpp`
  - Add an overload that accepts `InitialStarDetectionConfig` explicitly.
  - Keep existing overloads as wrappers that call `currentInitialStarDetectionConfig()`.

- `src/CanvasWidgets.h/.cpp`
  - Add coarse-drift overlay data and drawing to `FullFrameCanvas`.

- `src/DIMM.h`
  - Add coarse action/button/controller/runtime members and private slots/helpers.

- `src/DIMM.cpp`
  - Register metatypes, create/connect `AlignmentCoarseController`, add toolbar/menu action.

- `src/DIMM.Ui.cpp`
  - Add embedded coarse button and refresh enabled/visible state.

- `src/DIMM.Alignment.cpp`
  - Add coarse mode lifecycle, submit frames from `handleAlignmentFramePacket`, consume estimates, update overlay.

- `CMakeLists.txt`
  - Add the new files explicitly to the `srcs` list.

Tests:

- `tests/test_alignment_coarse_estimator_static.py`
- `tests/test_alignment_coarse_controller_static.py`
- `tests/test_alignment_coarse_ui_static.py`
- `tests/test_alignment_coarse_canvas_static.py`
- `tests/test_full_frame_star_detector_config_injection_static.py`

These Python tests are static guardrails only. They do not simulate C++ execution. They are useful for preventing missing files, wrong integration points, accidental `PolarisSolver` coupling, UI-thread detection, and lost Mono12/config behavior. The real functional check is the user’s local build and live-camera test.

---

## Task 1: Add Explicit Detector Config Overload

**Files:**
- Modify: `src/FullFrameStarDetector.h`
- Modify: `src/FullFrameStarDetector.cpp`
- Test: `tests/test_full_frame_star_detector_config_injection_static.py`

**Why:** Coarse drift runs in a worker thread. It must use the settings snapshot from the submitting UI thread, not read a mutable global config while processing.

- [ ] **Step 1: Write the failing static test**

Create `tests/test_full_frame_star_detector_config_injection_static.py`:

```python
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class FullFrameStarDetectorConfigInjectionStaticTest(unittest.TestCase):
    def test_detector_exposes_explicit_config_overload(self):
        header = read("src/FullFrameStarDetector.h")
        cpp = read("src/FullFrameStarDetector.cpp")

        self.assertIn('#include "InitialStarDetectionConfig.h"', header)
        self.assertIn(
            "detectInitialStarCandidates(const cv::Mat& grayscale,",
            header,
        )
        self.assertIn("const InitialStarDetectionConfig& config", header)
        self.assertIn(
            "detectInitialStarCandidates(const cv::Mat& grayscale,",
            cpp,
        )
        self.assertIn("const InitialStarDetectionConfig& config", cpp)
        self.assertIn(
            "return detectInitialStarCandidates(grayscale, currentInitialStarDetectionConfig(), peakValue, thresholdValue);",
            cpp,
        )

    def test_explicit_overload_uses_passed_config_for_threshold_and_area(self):
        cpp = read("src/FullFrameStarDetector.cpp")
        explicit_body = cpp.split(
            "QVector<InitialStarCandidate> detectInitialStarCandidates(const cv::Mat& grayscale,",
            1,
        )[1].split(
            "bool detectRawInitialStarPeakCandidate",
            1,
        )[0]

        self.assertIn("config.minimumIntensity", explicit_body)
        self.assertIn("config.sigmaThreshold", explicit_body)
        self.assertIn("config.peakFraction", explicit_body)
        self.assertIn("config.thresholdAbsolute", explicit_body)
        self.assertIn("area < config.minArea", explicit_body)
        self.assertIn("area > config.maxArea", explicit_body)
        self.assertNotIn("currentInitialStarDetectionConfig()", explicit_body)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and confirm it fails**

```powershell
python -m unittest tests.test_full_frame_star_detector_config_injection_static
```

Expected: fail because the explicit overload does not exist yet.

- [ ] **Step 3: Update `src/FullFrameStarDetector.h`**

Add the config include and overload. Keep the old signature.

```cpp
#include "InitialStarDetectionConfig.h"
```

Declare:

```cpp
QVector<PolarisDetectionPipeline::InitialStarCandidate> detectInitialStarCandidates(
    const cv::Mat& grayscale,
    const InitialStarDetectionConfig& config,
    double* peakValue = nullptr,
    double* thresholdValue = nullptr);

QVector<PolarisDetectionPipeline::InitialStarCandidate> detectInitialStarCandidates(
    const cv::Mat& grayscale,
    double* peakValue = nullptr,
    double* thresholdValue = nullptr);
```

- [ ] **Step 4: Update `src/FullFrameStarDetector.cpp`**

Change the current function into the explicit-config overload:

```cpp
QVector<InitialStarCandidate> detectInitialStarCandidates(const cv::Mat& grayscale,
                                                          const InitialStarDetectionConfig& config,
                                                          double* peakValue,
                                                          double* thresholdValue)
{
    QVector<InitialStarCandidate> candidates;
    if (grayscale.empty() || grayscale.channels() != 1) {
        return candidates;
    }

    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(grayscale, mean, stddev);

    double minValue = 0.0;
    double maxValue = 0.0;
    cv::minMaxLoc(grayscale, &minValue, &maxValue);
    if (peakValue) {
        *peakValue = maxValue;
    }

    const double dynamicThreshold = std::max({config.minimumIntensity,
                                              mean[0] + config.sigmaThreshold * stddev[0],
                                              mean[0] + (maxValue - mean[0]) * config.peakFraction});
    const double threshold = config.thresholdAbsolute >= 0.0
                                 ? std::min(config.thresholdAbsolute, dynamicThreshold)
                                 : dynamicThreshold;
    if (thresholdValue) {
        *thresholdValue = threshold;
    }
    if (maxValue <= threshold) {
        return candidates;
    }

    cv::Mat binary;
    cv::compare(grayscale, cv::Scalar(threshold), binary, cv::CMP_GT);

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int componentCount =
        cv::connectedComponentsWithStats(binary, labels, stats, centroids, 8, CV_32S);

    std::vector<double> componentSignal(static_cast<size_t>(componentCount), 0.0);
    std::vector<double> componentPeak(static_cast<size_t>(componentCount), 0.0);
    for (int y = 0; y < labels.rows; ++y) {
        const int* labelRow = labels.ptr<int>(y);
        for (int x = 0; x < labels.cols; ++x) {
            const int label = labelRow[x];
            if (label > 0 && label < componentCount) {
                const double value = rawPixelValueAt(grayscale, y, x);
                componentSignal[static_cast<size_t>(label)] += value;
                componentPeak[static_cast<size_t>(label)] =
                    std::max(componentPeak[static_cast<size_t>(label)], value);
            }
        }
    }

    for (int label = 1; label < componentCount; ++label) {
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        const int width = stats.at<int>(label, cv::CC_STAT_WIDTH);
        const int height = stats.at<int>(label, cv::CC_STAT_HEIGHT);
        if (area < config.minArea || area > config.maxArea) {
            continue;
        }

        InitialStarCandidate candidate;
        candidate.center = QPointF(centroids.at<double>(label, 0), centroids.at<double>(label, 1));
        candidate.area = area;
        candidate.peak = componentPeak[static_cast<size_t>(label)];
        candidate.signal = componentSignal[static_cast<size_t>(label)];
        candidate.bbox = QRect(stats.at<int>(label, cv::CC_STAT_LEFT),
                               stats.at<int>(label, cv::CC_STAT_TOP),
                               width,
                               height);
        candidates.append(candidate);
    }

    std::sort(candidates.begin(), candidates.end(), [](const InitialStarCandidate& a,
                                                       const InitialStarCandidate& b) {
        return a.signal > b.signal;
    });
    for (int i = 0; i < candidates.size(); ++i) {
        candidates[i].index = i + 1;
    }

    return candidates;
}
```

Then add the old wrapper below it:

```cpp
QVector<InitialStarCandidate> detectInitialStarCandidates(const cv::Mat& grayscale,
                                                          double* peakValue,
                                                          double* thresholdValue)
{
    return detectInitialStarCandidates(grayscale,
                                       currentInitialStarDetectionConfig(),
                                       peakValue,
                                       thresholdValue);
}
```

- [ ] **Step 5: Run focused static tests**

```powershell
python -m unittest tests.test_full_frame_star_detector_config_injection_static tests.test_full_frame_star_detection_settings_static
```

Expected: OK.

- [ ] **Step 6: Commit**

```powershell
git add src/FullFrameStarDetector.h src/FullFrameStarDetector.cpp tests/test_full_frame_star_detector_config_injection_static.py
git commit -m "feat: allow explicit full-frame star detection config"
```

---

## Task 2: Add Pure Coarse Drift Estimator

**Files:**
- Create: `src/AlignmentCoarseEstimator.h`
- Create: `src/AlignmentCoarseEstimator.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_alignment_coarse_estimator_static.py`

**Why:** Keep the math independent from UI and threads. This makes the feature understandable and avoids touching `PolarisSolver`.

- [ ] **Step 1: Write the failing static test**

Create `tests/test_alignment_coarse_estimator_static.py`:

```python
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class AlignmentCoarseEstimatorStaticTest(unittest.TestCase):
    def test_estimator_files_are_build_units_and_do_not_use_polaris_solver(self):
        self.assertTrue((ROOT / "src/AlignmentCoarseEstimator.h").exists())
        self.assertTrue((ROOT / "src/AlignmentCoarseEstimator.cpp").exists())
        cmake = read("CMakeLists.txt")
        header = read("src/AlignmentCoarseEstimator.h")
        cpp = read("src/AlignmentCoarseEstimator.cpp")

        self.assertIn("src/AlignmentCoarseEstimator.h", cmake)
        self.assertIn("src/AlignmentCoarseEstimator.cpp", cmake)
        self.assertNotIn("PolarisSolver", header)
        self.assertNotIn("PolarisSolver", cpp)
        self.assertIn("struct CoarseAlignmentConfig", header)
        self.assertIn("struct CoarseAlignmentEstimate", header)
        self.assertIn("class AlignmentCoarseTracker", header)

    def test_estimator_computes_velocity_and_ncp_center(self):
        cpp = read("src/AlignmentCoarseEstimator.cpp")
        self.assertIn("fitTrackVelocity", cpp)
        self.assertIn("vxNumerator", cpp)
        self.assertIn("vyNumerator", cpp)
        self.assertIn("durationSec", cpp)
        self.assertIn("speedPxSec", cpp)
        self.assertIn("solveNorthCelestialPoleCenter", cpp)
        self.assertIn("a00 += weight * vx * vx", cpp)
        self.assertIn("a01 += weight * vx * vy", cpp)
        self.assertIn("a11 += weight * vy * vy", cpp)
        self.assertIn("det = a00 * a11 - a01 * a01", cpp)
        self.assertIn("offsetDeg", cpp)
        self.assertIn("siderealArcsecSec", cpp)

    def test_estimator_has_candidate_and_track_limits(self):
        header = read("src/AlignmentCoarseEstimator.h")
        cpp = read("src/AlignmentCoarseEstimator.cpp")
        for token in [
            "maxCandidates",
            "maxAssociationDistancePx",
            "minTrackDurationSec",
            "minTrackPoints",
            "minTrackDisplacementPx",
            "maxTrackFitRmsPx",
            "maxStaleTrackSec",
        ]:
            self.assertIn(token, header)
            self.assertIn(token, cpp)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and confirm it fails**

```powershell
python -m unittest tests.test_alignment_coarse_estimator_static
```

Expected: fail because files do not exist.

- [ ] **Step 3: Create `src/AlignmentCoarseEstimator.h`**

Use this interface:

```cpp
#pragma once

#include "PolarisDetectionPipeline.h"

#include <QPointF>
#include <QRectF>
#include <QMetaType>
#include <QSize>
#include <QString>
#include <QVector>

struct CoarseAlignmentConfig {
    int maxCandidates = 80;
    double maxAssociationDistancePx = 25.0;
    double maxStaleTrackSec = 5.0;
    int maxTrackPoints = 90;
    int minTrackPoints = 5;
    double minTrackDurationSec = 15.0;
    double minTrackDisplacementPx = 2.0;
    double maxTrackFitRmsPx = 3.5;
    double maxCenterResidualRmsPx = 80.0;
    double plateScaleArcsecPx = 1.917;
    double siderealArcsecSec = 15.041;
    int minTracksForCenter = 2;
};

struct CoarseAlignmentTrackOverlay {
    int id = 0;
    QPointF startPx;
    QPointF endPx;
    QPointF velocityPxSec;
    double speedPxSec = 0.0;
    double durationSec = 0.0;
    double fitRmsPx = 0.0;
    bool usedForSolve = false;
};

struct CoarseAlignmentEstimate {
    int cameraIndex = 0;
    quint64 generation = 0;
    quint64 frameId = 0;
    bool valid = false;
    bool centerIllConditioned = false;
    bool tooFewTracks = false;
    bool tooManyCandidates = false;
    QSize frameSize;
    QPointF northCelestialPolePx;
    QPointF frameCenterPx;
    QPointF adjustmentVectorPx;
    double offsetPx = 0.0;
    double offsetDeg = 0.0;
    double medianSpeedPxSec = 0.0;
    double medianPolarDistanceDegFromSpeed = 0.0;
    double centerResidualRmsPx = 0.0;
    int detectedCandidateCount = 0;
    int acceptedCandidateCount = 0;
    int activeTrackCount = 0;
    int usableTrackCount = 0;
    double processingMs = 0.0;
    QString statusText;
    QVector<CoarseAlignmentTrackOverlay> tracks;
};

class AlignmentCoarseTracker final {
public:
    struct TrackPoint {
        qint64 timestampMs = 0;
        QPointF positionPx;
        double signal = 0.0;
    };

    struct Track {
        int id = 0;
        QVector<TrackPoint> points;
        QPointF velocityPxSec;
        double speedPxSec = 0.0;
        double durationSec = 0.0;
        double displacementPx = 0.0;
        double fitRmsPx = 0.0;
        qint64 lastTimestampMs = 0;
        bool usedForSolve = false;
    };

    void reset();
    CoarseAlignmentEstimate addFrame(int cameraIndex,
                                     quint64 generation,
                                     quint64 frameId,
                                     qint64 timestampMs,
                                     const QSize& frameSize,
                                     QVector<PolarisDetectionPipeline::InitialStarCandidate> candidates,
                                     const CoarseAlignmentConfig& config);

private:
    int m_nextTrackId = 1;
    QVector<Track> m_tracks;
};

Q_DECLARE_METATYPE(CoarseAlignmentEstimate)
```

- [ ] **Step 4: Create `src/AlignmentCoarseEstimator.cpp`**

Implement these private helpers in the anonymous namespace:

```cpp
namespace {

double pointDistance(const QPointF& a, const QPointF& b)
{
    const double dx = a.x() - b.x();
    const double dy = a.y() - b.y();
    return std::sqrt(dx * dx + dy * dy);
}

double medianOf(QVector<double> values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const int mid = values.size() / 2;
    return values.size() % 2 == 0 ? 0.5 * (values[mid - 1] + values[mid]) : values[mid];
}

QVector<PolarisDetectionPipeline::InitialStarCandidate> limitedCandidates(
    QVector<PolarisDetectionPipeline::InitialStarCandidate> candidates,
    int maxCandidates)
{
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) {
                  return a.signal == b.signal ? a.peak > b.peak : a.signal > b.signal;
              });
    if (candidates.size() > maxCandidates) {
        candidates.resize(maxCandidates);
    }
    for (int i = 0; i < candidates.size(); ++i) {
        candidates[i].index = i + 1;
    }
    return candidates;
}
```

Velocity fitting must use centroid positions and timestamps:

```cpp
bool fitTrackVelocity(AlignmentCoarseTracker::Track* track)
{
    if (!track || track->points.size() < 2) {
        return false;
    }

    const qint64 t0Ms = track->points.first().timestampMs;
    double meanT = 0.0;
    double meanX = 0.0;
    double meanY = 0.0;
    for (const auto& point : track->points) {
        const double t = static_cast<double>(point.timestampMs - t0Ms) / 1000.0;
        meanT += t;
        meanX += point.positionPx.x();
        meanY += point.positionPx.y();
    }
    const double n = static_cast<double>(track->points.size());
    meanT /= n;
    meanX /= n;
    meanY /= n;

    double denominator = 0.0;
    double vxNumerator = 0.0;
    double vyNumerator = 0.0;
    for (const auto& point : track->points) {
        const double t = static_cast<double>(point.timestampMs - t0Ms) / 1000.0;
        const double dt = t - meanT;
        denominator += dt * dt;
        vxNumerator += dt * (point.positionPx.x() - meanX);
        vyNumerator += dt * (point.positionPx.y() - meanY);
    }
    if (denominator <= 1e-9) {
        return false;
    }

    const double vx = vxNumerator / denominator;
    const double vy = vyNumerator / denominator;
    track->velocityPxSec = QPointF(vx, vy);
    track->speedPxSec = std::sqrt(vx * vx + vy * vy);
    track->durationSec =
        static_cast<double>(track->points.last().timestampMs - track->points.first().timestampMs) / 1000.0;
    track->displacementPx = pointDistance(track->points.first().positionPx,
                                          track->points.last().positionPx);

    double residualSum = 0.0;
    for (const auto& point : track->points) {
        const double t = static_cast<double>(point.timestampMs - t0Ms) / 1000.0;
        const double predictedX = meanX + vx * (t - meanT);
        const double predictedY = meanY + vy * (t - meanT);
        const double dx = point.positionPx.x() - predictedX;
        const double dy = point.positionPx.y() - predictedY;
        residualSum += dx * dx + dy * dy;
    }
    track->fitRmsPx = std::sqrt(residualSum / n);
    return true;
}
```

NCP center solve must use:

```text
(p - C) dot v = 0
=> vx * Cx + vy * Cy = vx * px + vy * py
```

Implement:

```cpp
bool solveNorthCelestialPoleCenter(const QVector<AlignmentCoarseTracker::Track*>& tracks,
                                   const CoarseAlignmentConfig& config,
                                   QPointF* center,
                                   double* residualRmsPx)
{
    double a00 = 0.0;
    double a01 = 0.0;
    double a11 = 0.0;
    double b0 = 0.0;
    double b1 = 0.0;

    for (const auto* track : tracks) {
        const QPointF p = track->points[track->points.size() / 2].positionPx;
        const double vx = track->velocityPxSec.x();
        const double vy = track->velocityPxSec.y();
        const double rhs = vx * p.x() + vy * p.y();
        const double weight = std::max(0.1, track->durationSec) /
                              (1.0 + track->fitRmsPx * track->fitRmsPx);
        a00 += weight * vx * vx;
        a01 += weight * vx * vy;
        a11 += weight * vy * vy;
        b0 += weight * vx * rhs;
        b1 += weight * vy * rhs;
    }

    const double det = a00 * a11 - a01 * a01;
    if (std::abs(det) < 1e-9) {
        return false;
    }

    const double cx = (b0 * a11 - b1 * a01) / det;
    const double cy = (a00 * b1 - a01 * b0) / det;
    *center = QPointF(cx, cy);

    double residualSum = 0.0;
    for (const auto* track : tracks) {
        const QPointF p = track->points[track->points.size() / 2].positionPx;
        const double speed = std::max(1e-9, track->speedPxSec);
        const double nx = track->velocityPxSec.x() / speed;
        const double ny = track->velocityPxSec.y() / speed;
        const double residual = (p.x() - cx) * nx + (p.y() - cy) * ny;
        residualSum += residual * residual;
    }
    *residualRmsPx = std::sqrt(residualSum / std::max(1, tracks.size()));
    return true;
}
```

`AlignmentCoarseTracker::addFrame` should:

1. Limit candidates to `config.maxCandidates`.
2. Associate each candidate to the nearest unassigned active track if distance <= `config.maxAssociationDistancePx`.
3. Create new tracks for unassigned candidates.
4. Trim each track to `config.maxTrackPoints`.
5. Remove stale tracks older than `config.maxStaleTrackSec`.
6. Fit velocity for all tracks with at least 2 points.
7. Select usable tracks:
   - `points.size() >= config.minTrackPoints`
   - `durationSec >= config.minTrackDurationSec`
   - `displacementPx >= config.minTrackDisplacementPx`
   - `fitRmsPx <= config.maxTrackFitRmsPx`
   - `speedPxSec > 0.005`
8. If enough usable tracks, solve NCP center.
9. Fill `CoarseAlignmentEstimate`.

Status logic:

```cpp
if (estimate.detectedCandidateCount > config.maxCandidates) {
    estimate.tooManyCandidates = true;
}
if (usableTracks.size() < config.minTracksForCenter) {
    estimate.tooFewTracks = true;
    estimate.statusText = QStringLiteral("粗对准: 采样中 %1/%2 条轨迹，请等待或关闭跟踪")
                              .arg(usableTracks.size())
                              .arg(config.minTracksForCenter);
} else if (!solvedCenter) {
    estimate.centerIllConditioned = true;
    estimate.statusText = QStringLiteral("粗对准: 轨迹方向接近平行，请延长采样或稍微移动视场");
} else if (estimate.centerResidualRmsPx > config.maxCenterResidualRmsPx) {
    estimate.statusText = QStringLiteral("粗对准: 圆心估计不稳定 RMS %1 px")
                              .arg(estimate.centerResidualRmsPx, 0, 'f', 1);
} else {
    estimate.valid = true;
    estimate.statusText = QStringLiteral("粗对准: 北天极距中心 %1 px / %2°")
                              .arg(estimate.offsetPx, 0, 'f', 0)
                              .arg(estimate.offsetDeg, 0, 'f', 2);
}
```

Angular fields:

```cpp
estimate.frameCenterPx = QPointF(frameSize.width() * 0.5, frameSize.height() * 0.5);
estimate.adjustmentVectorPx = estimate.northCelestialPolePx - estimate.frameCenterPx;
estimate.offsetPx = pointDistance(estimate.northCelestialPolePx, estimate.frameCenterPx);
estimate.offsetDeg = estimate.offsetPx * config.plateScaleArcsecPx / 3600.0;

const double medianSpeedArcsecSec = estimate.medianSpeedPxSec * config.plateScaleArcsecPx;
const double ratio = std::clamp(medianSpeedArcsecSec / config.siderealArcsecSec, 0.0, 1.0);
estimate.medianPolarDistanceDegFromSpeed = qRadiansToDegrees(std::asin(ratio));
```

- [ ] **Step 5: Update `CMakeLists.txt`**

Add these entries inside `list(APPEND srcs ...)`:

```cmake
    src/AlignmentCoarseEstimator.h
    src/AlignmentCoarseEstimator.cpp
```

- [ ] **Step 6: Run the focused static test**

```powershell
python -m unittest tests.test_alignment_coarse_estimator_static
```

Expected: OK.

- [ ] **Step 7: Commit**

```powershell
git add src/AlignmentCoarseEstimator.h src/AlignmentCoarseEstimator.cpp CMakeLists.txt tests/test_alignment_coarse_estimator_static.py
git commit -m "feat: add coarse alignment drift estimator"
```

---

## Task 3: Add Background Coarse Controller

**Files:**
- Create: `src/AlignmentCoarseController.h`
- Create: `src/AlignmentCoarseController.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_alignment_coarse_controller_static.py`

**Why:** Full-frame detection on 5120 x 5120 images can be expensive. It must not run on the UI thread.

- [ ] **Step 1: Write the failing static test**

Create `tests/test_alignment_coarse_controller_static.py`:

```python
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class AlignmentCoarseControllerStaticTest(unittest.TestCase):
    def test_controller_is_threaded_latest_only_queue(self):
        self.assertTrue((ROOT / "src/AlignmentCoarseController.h").exists())
        self.assertTrue((ROOT / "src/AlignmentCoarseController.cpp").exists())
        cmake = read("CMakeLists.txt")
        header = read("src/AlignmentCoarseController.h")
        cpp = read("src/AlignmentCoarseController.cpp")

        self.assertIn("src/AlignmentCoarseController.h", cmake)
        self.assertIn("src/AlignmentCoarseController.cpp", cmake)
        self.assertIn("class AlignmentCoarseController", header)
        self.assertIn("QThread", header)
        self.assertIn("submitFrame", header)
        self.assertIn("estimateReady", header)
        self.assertIn("PendingFrame", cpp)
        self.assertIn("m_pendingLatest", cpp)
        self.assertIn("m_taskRunning", cpp)
        self.assertIn("startCoarseTask", cpp)

    def test_controller_uses_mono12_raw_and_explicit_detection_config(self):
        cpp = read("src/AlignmentCoarseController.cpp")
        self.assertIn("ImageUtils::grayscaleDetectionFrame", cpp)
        self.assertIn("detectInitialStarCandidates(grayscale, task.starConfig", cpp)
        self.assertNotIn("normalizeMono8Frame", cpp)
        self.assertNotIn("PolarisSolver", cpp)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and confirm it fails**

```powershell
python -m unittest tests.test_alignment_coarse_controller_static
```

Expected: fail because controller files do not exist.

- [ ] **Step 3: Create `src/AlignmentCoarseController.h`**

Use this interface:

```cpp
#pragma once

#include "AlignmentCoarseEstimator.h"
#include "InitialStarDetectionConfig.h"

#include <QObject>
#include <QThread>

#include <opencv2/core/mat.hpp>

class AlignmentCoarseWorker;

class AlignmentCoarseController : public QObject {
    Q_OBJECT

public:
    explicit AlignmentCoarseController(QObject* parent = nullptr);
    ~AlignmentCoarseController() override;

    void submitFrame(int cameraIndex,
                     const cv::Mat& frame,
                     const InitialStarDetectionConfig& starConfig,
                     const CoarseAlignmentConfig& coarseConfig,
                     quint64 generation,
                     quint64 frameId,
                     qint64 timestampMs);
    void resetCamera(int cameraIndex);
    void resetAll();
    void cancelAll(quint64 generation);

signals:
    void estimateReady(CoarseAlignmentEstimate estimate);

private:
    QThread* m_workerThread = nullptr;
    AlignmentCoarseWorker* m_worker = nullptr;
};
```

- [ ] **Step 4: Create `src/AlignmentCoarseController.cpp`**

Implementation requirements:

- The worker owns `AlignmentCoarseTracker m_trackers[2]`.
- It stores latest pending frame per camera.
- If one task is running for a camera and a new frame arrives, replace only that camera’s pending frame.
- When a task finishes, immediately start the pending latest task for that camera if present.
- `processTask` must:
  - Convert raw frame with `ImageUtils::grayscaleDetectionFrame`.
  - Call `detectInitialStarCandidates(grayscale, task.starConfig, &peak, &threshold)`.
  - Pass candidates to `m_trackers[cameraIndex].addFrame`.
  - Set `processingMs`.
  - Emit `estimateReady`.

Worker skeleton:

```cpp
namespace {

class AlignmentCoarseWorker : public QObject {
    Q_OBJECT

public:
    struct PendingFrame {
        bool valid = false;
        int cameraIndex = 0;
        cv::Mat frame;
        InitialStarDetectionConfig starConfig;
        CoarseAlignmentConfig coarseConfig;
        quint64 generation = 0;
        quint64 frameId = 0;
        qint64 timestampMs = 0;
    };

    void submit(PendingFrame task);
    void resetCamera(int cameraIndex);
    void resetAll();
    void cancelAll(quint64 generation);

signals:
    void estimateReady(CoarseAlignmentEstimate estimate);

private:
    void startCoarseTask(const PendingFrame& task);
    CoarseAlignmentEstimate processTask(const PendingFrame& task);

    quint64 m_generation = 0;
    bool m_taskRunning[2] = {false, false};
    PendingFrame m_pendingLatest[2];
    AlignmentCoarseTracker m_trackers[2];
};

} // namespace
```

Use queued invocation from controller to worker:

```cpp
QMetaObject::invokeMethod(m_worker,
                          [worker = m_worker, task]() mutable {
                              worker->submit(std::move(task));
                          },
                          Qt::QueuedConnection);
```

Do not use `std::async`, `QtConcurrent`, or direct UI-thread calls.

- [ ] **Step 5: Register generated moc for nested Worker**

At bottom of `src/AlignmentCoarseController.cpp`, add:

```cpp
#include "AlignmentCoarseController.moc"
```

This is needed because `AlignmentCoarseWorker` has `Q_OBJECT` inside the `.cpp`.

- [ ] **Step 6: Update `CMakeLists.txt`**

Add:

```cmake
    src/AlignmentCoarseController.h
    src/AlignmentCoarseController.cpp
```

- [ ] **Step 7: Run focused static tests**

```powershell
python -m unittest tests.test_alignment_coarse_estimator_static tests.test_alignment_coarse_controller_static tests.test_full_frame_star_detector_config_injection_static
```

Expected: OK.

- [ ] **Step 8: Commit**

```powershell
git add src/AlignmentCoarseController.h src/AlignmentCoarseController.cpp CMakeLists.txt tests/test_alignment_coarse_controller_static.py
git commit -m "feat: process coarse alignment drift off the UI thread"
```

---

## Task 4: Add Coarse Overlay to FullFrameCanvas

**Files:**
- Modify: `src/CanvasWidgets.h`
- Modify: `src/CanvasWidgets.cpp`
- Test: `tests/test_alignment_coarse_canvas_static.py`

**Why:** The user needs visual feedback: measured drift trails, estimated NCP, direction arrow, and quality/status.

- [ ] **Step 1: Write the failing static test**

Create `tests/test_alignment_coarse_canvas_static.py`:

```python
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class AlignmentCoarseCanvasStaticTest(unittest.TestCase):
    def test_canvas_exposes_and_draws_coarse_overlay(self):
        header = read("src/CanvasWidgets.h")
        cpp = read("src/CanvasWidgets.cpp")

        self.assertIn("struct CoarseDriftTrackOverlay", header)
        self.assertIn("struct CoarseDriftOverlay", header)
        self.assertIn("setCoarseDriftOverlay", header)
        self.assertIn("clearCoarseDriftOverlay", header)
        self.assertIn("drawCoarseDriftOverlay", header)
        self.assertIn("m_coarseDriftOverlay", header)

        self.assertIn("FullFrameCanvas::setCoarseDriftOverlay", cpp)
        self.assertIn("FullFrameCanvas::clearCoarseDriftOverlay", cpp)
        self.assertIn("FullFrameCanvas::drawCoarseDriftOverlay", cpp)
        self.assertIn("drawCoarseDriftOverlay(painter)", cpp)
        self.assertIn("northCelestialPolePx", cpp)
        self.assertIn("adjustmentVectorPx", cpp)
        self.assertIn("drawLine", cpp)
        self.assertIn("drawEllipse", cpp)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and confirm it fails**

```powershell
python -m unittest tests.test_alignment_coarse_canvas_static
```

Expected: fail because overlay is not present.

- [ ] **Step 3: Update `src/CanvasWidgets.h`**

Add these structs to `FullFrameCanvas` public section:

```cpp
struct CoarseDriftTrackOverlay {
    QPointF startPx;
    QPointF endPx;
    QPointF velocityPxSec;
    double speedPxSec = 0.0;
    bool usedForSolve = false;
};

struct CoarseDriftOverlay {
    bool enabled = false;
    bool valid = false;
    QPointF northCelestialPolePx;
    QPointF frameCenterPx;
    QPointF adjustmentVectorPx;
    double offsetPx = 0.0;
    double offsetDeg = 0.0;
    double medianSpeedPxSec = 0.0;
    double centerResidualRmsPx = 0.0;
    int detectedCandidateCount = 0;
    int usableTrackCount = 0;
    QString statusText;
    QVector<CoarseDriftTrackOverlay> tracks;
};
```

Add public methods:

```cpp
void setCoarseDriftOverlay(const CoarseDriftOverlay& overlay);
void clearCoarseDriftOverlay();
```

Add private member and draw method:

```cpp
CoarseDriftOverlay m_coarseDriftOverlay;
void drawCoarseDriftOverlay(QPainter& painter);
```

- [ ] **Step 4: Update `src/CanvasWidgets.cpp` setters/clear**

Add:

```cpp
void FullFrameCanvas::setCoarseDriftOverlay(const CoarseDriftOverlay& overlay)
{
    m_coarseDriftOverlay = overlay;
    update();
}

void FullFrameCanvas::clearCoarseDriftOverlay()
{
    m_coarseDriftOverlay = CoarseDriftOverlay();
    update();
}
```

In `FullFrameCanvas::clear()`, also reset:

```cpp
m_coarseDriftOverlay = CoarseDriftOverlay();
```

In `paintEvent`, call after `drawAlignmentOverlay(painter)` and before ROI/candidates:

```cpp
drawCoarseDriftOverlay(painter);
```

- [ ] **Step 5: Implement `drawCoarseDriftOverlay`**

Draw only when enabled and image exists. Use image-to-widget conversion.

Required drawing behavior:

- Track lines:
  - Used tracks: green/cyan.
  - Unused active tracks: dim gray.
- Estimated NCP:
  - Valid center: green cross and small circle.
  - Invalid center: do not draw the NCP cross.
- Adjustment arrow:
  - From frame center to estimated NCP.
  - If NCP is offscreen, clamp arrow endpoint to image rect edge.
- Text:
  - Show `statusText`.
  - Show `候选`, `轨迹`, `速度`, `RMS`.

Use this structure:

```cpp
void FullFrameCanvas::drawCoarseDriftOverlay(QPainter& painter)
{
    if (!m_coarseDriftOverlay.enabled || m_image.empty() || m_scale <= 0.0) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF imageRect(m_offset.x(),
                           m_offset.y(),
                           m_qimage.width() * m_scale,
                           m_qimage.height() * m_scale);

    for (const CoarseDriftTrackOverlay& track : m_coarseDriftOverlay.tracks) {
        const QPointF start = imageToWidget(track.startPx);
        const QPointF end = imageToWidget(track.endPx);
        painter.setPen(track.usedForSolve
                           ? QPen(QColor(120, 235, 190, 210), 1.6)
                           : QPen(QColor(150, 150, 150, 120), 1.0));
        painter.drawLine(start, end);
        painter.drawEllipse(end, track.usedForSolve ? 3.5 : 2.0, track.usedForSolve ? 3.5 : 2.0);
    }

    if (m_coarseDriftOverlay.valid) {
        const QPointF center = imageToWidget(m_coarseDriftOverlay.northCelestialPolePx);
        const QPointF frameCenter = imageToWidget(m_coarseDriftOverlay.frameCenterPx);
        painter.setPen(QPen(QColor(120, 255, 160), 2.0));
        painter.drawEllipse(center, 8.0, 8.0);
        painter.drawLine(center + QPointF(-14.0, 0.0), center + QPointF(14.0, 0.0));
        painter.drawLine(center + QPointF(0.0, -14.0), center + QPointF(0.0, 14.0));

        painter.setPen(QPen(QColor(255, 220, 90), 2.0, Qt::DashLine));
        painter.drawLine(frameCenter, center);
    }

    QStringList lines;
    if (!m_coarseDriftOverlay.statusText.isEmpty()) {
        lines << m_coarseDriftOverlay.statusText;
    }
    lines << QStringLiteral("候选 %1 | 轨迹 %2 | 速度 %3 px/s | RMS %4 px")
                 .arg(m_coarseDriftOverlay.detectedCandidateCount)
                 .arg(m_coarseDriftOverlay.usableTrackCount)
                 .arg(m_coarseDriftOverlay.medianSpeedPxSec, 0, 'f', 3)
                 .arg(m_coarseDriftOverlay.centerResidualRmsPx, 0, 'f', 1);

    painter.setFont(QFont("Microsoft YaHei", 9));
    painter.setPen(m_coarseDriftOverlay.valid ? QColor(170, 255, 190) : QColor(255, 210, 90));
    painter.drawText(imageRect.adjusted(12.0, 70.0, -12.0, -12.0).topLeft(),
                     lines.join(QLatin1String(" | ")));

    painter.restore();
}
```

Keep the text concise because this is drawn inside the image area.

- [ ] **Step 6: Run the focused static test**

```powershell
python -m unittest tests.test_alignment_coarse_canvas_static tests.test_canvas_mono12_display_static
```

Expected: OK. If `test_canvas_mono12_display_static` already fails from pre-existing canvas behavior, report it instead of forcing unrelated display changes.

- [ ] **Step 7: Commit**

```powershell
git add src/CanvasWidgets.h src/CanvasWidgets.cpp tests/test_alignment_coarse_canvas_static.py
git commit -m "feat: draw coarse alignment drift overlay"
```

---

## Task 5: Wire Coarse State Into DIMM Alignment Mode

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.cpp`
- Modify: `src/DIMM.Ui.cpp`
- Modify: `src/DIMM.Alignment.cpp`
- Test: `tests/test_alignment_coarse_ui_static.py`

**Why:** Add the user-facing “粗对准” control and route alignment frames to the coarse controller when active.

- [ ] **Step 1: Write the failing static test**

Create `tests/test_alignment_coarse_ui_static.py`:

```python
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class AlignmentCoarseUiStaticTest(unittest.TestCase):
    def test_dimm_owns_coarse_runtime_and_controller(self):
        header = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.cpp")

        self.assertIn('#include "AlignmentCoarseEstimator.h"', header)
        self.assertIn("class AlignmentCoarseController", header)
        self.assertIn("void onToggleCoarseAlignment()", header)
        self.assertIn("void onCoarseAlignmentEstimateReady", header)
        self.assertIn("bool m_alignmentCoarseActive", header)
        self.assertIn("AlignmentCoarseController* m_alignmentCoarseController", header)
        self.assertIn("CoarseAlignmentEstimate m_alignmentCoarseEstimates", header)

        self.assertIn('qRegisterMetaType<CoarseAlignmentEstimate>("CoarseAlignmentEstimate")', dimm_cpp)
        self.assertIn("new AlignmentCoarseController(this)", dimm_cpp)
        self.assertIn("AlignmentCoarseController::estimateReady", dimm_cpp)

    def test_ui_adds_coarse_button_and_action(self):
        dimm_cpp = read("src/DIMM.cpp")
        ui_cpp = read("src/DIMM.Ui.cpp")

        self.assertIn("m_actionToggleCoarseAlignment", dimm_cpp)
        self.assertIn("btnToggleCoarseAlignment", dimm_cpp)
        self.assertIn("onToggleCoarseAlignment", dimm_cpp)
        self.assertIn("m_btnToggleCoarseAlignment", ui_cpp)
        self.assertIn("开始粗对准", ui_cpp)
        self.assertIn("停止粗对准", ui_cpp)
        self.assertIn("m_actionToggleCoarseAlignment->trigger()", ui_cpp)

    def test_alignment_frame_packet_routes_to_coarse_before_polaris_solver(self):
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        packet_body = alignment_cpp.split("void DIMM::handleAlignmentFramePacket", 1)[1].split(
            "bool DIMM::handleManualAlignmentFrameTracking",
            1,
        )[0]

        self.assertIn("m_alignmentCoarseActive", packet_body)
        self.assertIn("submitCoarseAlignmentFrame(cameraIndex, packet", packet_body)
        self.assertIn("finishAlignmentFramePreview(cameraIndex, packet, nowMs)", packet_body)
        self.assertLess(
            packet_body.index("m_alignmentCoarseActive"),
            packet_body.index("AlignmentFrameCoordinator::nextFrameAction"),
        )
        self.assertNotIn("PolarisSolver", packet_body.split("m_alignmentCoarseActive", 1)[1].split(
            "AlignmentFrameCoordinator::nextFrameAction",
            1,
        )[0])

    def test_coarse_start_cancels_polaris_solves_and_resets_tracking(self):
        alignment_cpp = read("src/DIMM.Alignment.cpp")
        body = alignment_cpp.split("void DIMM::onToggleCoarseAlignment", 1)[1].split(
            "void DIMM::resetCoarseAlignmentRuntime",
            1,
        )[0]
        self.assertIn("m_polarisSolverController->cancelAll", body)
        self.assertIn("resetCoarseAlignmentRuntime()", body)
        self.assertIn("m_alignmentCoarseActive = true", body)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and confirm it fails**

```powershell
python -m unittest tests.test_alignment_coarse_ui_static
```

Expected: fail because coarse UI/state is missing.

- [ ] **Step 3: Update `src/DIMM.h` includes and declarations**

Add:

```cpp
#include "AlignmentCoarseEstimator.h"
```

Forward declare:

```cpp
class AlignmentCoarseController;
```

Add slot:

```cpp
void onToggleCoarseAlignment();
```

Add private helpers near other alignment helpers:

```cpp
void resetCoarseAlignmentRuntime();
void clearCoarseAlignmentOverlays();
CoarseAlignmentConfig buildCoarseAlignmentConfig() const;
void submitCoarseAlignmentFrame(int cameraIndex, const CameraFrame& packet, qint64 nowMs);
void onCoarseAlignmentEstimateReady(CoarseAlignmentEstimate estimate);
void updateCoarseAlignmentOverlay(int cameraIndex);
```

Add members near alignment actions/buttons:

```cpp
QAction* m_actionToggleCoarseAlignment = nullptr;
QPushButton* m_btnToggleCoarseAlignment = nullptr;
```

Add members near alignment runtime:

```cpp
bool m_alignmentCoarseActive = false;
qint64 m_alignmentLastCoarseSubmitMs[kCameraCount] = {-1, -1};
int m_alignmentCoarseSubmitIntervalMs = 1000;
CoarseAlignmentEstimate m_alignmentCoarseEstimates[kCameraCount];
AlignmentCoarseController* m_alignmentCoarseController = nullptr;
```

- [ ] **Step 4: Update `src/DIMM.cpp`**

Add include:

```cpp
#include "AlignmentCoarseController.h"
```

In `registerMetaTypes()`:

```cpp
qRegisterMetaType<CoarseAlignmentEstimate>("CoarseAlignmentEstimate");
```

In `setupServiceManagers()` after `m_polarisSolverController` setup:

```cpp
m_alignmentCoarseController = new AlignmentCoarseController(this);
connect(m_alignmentCoarseController,
        &AlignmentCoarseController::estimateReady,
        this,
        &DIMM::onCoarseAlignmentEstimateReady,
        Qt::QueuedConnection);
```

In `setupRuntimeActions()` create action after `m_actionAlignmentMode`:

```cpp
m_actionToggleCoarseAlignment = new QAction(QStringLiteral("开始粗对准"), this);
m_actionToggleCoarseAlignment->setObjectName(QStringLiteral("btnToggleCoarseAlignment"));
m_actionToggleCoarseAlignment->setCheckable(true);
if (ui->toolbar) {
    ui->toolbar->insertAction(ui->btnSettings, m_actionToggleCoarseAlignment);
}
if (ui->menuTools) {
    ui->menuTools->insertAction(ui->actionROISchedule, m_actionToggleCoarseAlignment);
}
```

In `setupConnections()`:

```cpp
connect(m_actionToggleCoarseAlignment,
        &QAction::triggered,
        this,
        &DIMM::onToggleCoarseAlignment);
```

- [ ] **Step 5: Update `src/DIMM.Ui.cpp` embedded button**

In `setupFullFramePreviewCanvases()`, after `m_btnRetryBothPolarisSolve`:

```cpp
m_btnToggleCoarseAlignment = new QPushButton(QStringLiteral("开始粗对准"), ui->previewCanvas);
m_btnToggleCoarseAlignment->setVisible(false);
previewCanvasLayout->addWidget(m_btnToggleCoarseAlignment);
connect(m_btnToggleCoarseAlignment, &QPushButton::clicked, this, [this]() {
    if (m_actionToggleCoarseAlignment) {
        m_actionToggleCoarseAlignment->trigger();
    }
});
```

In `refreshActionStates()`:

```cpp
if (m_actionToggleCoarseAlignment) {
    const bool alignmentActive = m_captureState == CaptureState::Alignment;
    m_actionToggleCoarseAlignment->setEnabled(alignmentActive && !busy);
    m_actionToggleCoarseAlignment->setChecked(m_alignmentCoarseActive);
    m_actionToggleCoarseAlignment->setText(m_alignmentCoarseActive
                                               ? QStringLiteral("停止粗对准")
                                               : QStringLiteral("开始粗对准"));
}
```

When enabling confirm/retry actions, disable them during coarse drift:

```cpp
m_actionConfirmCamera1Polaris->setEnabled(m_captureState == CaptureState::Alignment &&
                                          !busy &&
                                          !m_alignmentCoarseActive);
```

Apply the same `!m_alignmentCoarseActive` condition to:

- `m_actionConfirmCamera2Polaris`
- `m_actionRetryCamera1PolarisSolve`
- `m_actionRetryCamera2PolarisSolve`
- `m_actionRetryBothPolarisSolve`

For the embedded coarse button:

```cpp
if (m_btnToggleCoarseAlignment) {
    m_btnToggleCoarseAlignment->setVisible(alignmentControlsVisible);
    m_btnToggleCoarseAlignment->setEnabled(m_actionToggleCoarseAlignment &&
                                           m_actionToggleCoarseAlignment->isEnabled());
    m_btnToggleCoarseAlignment->setText(m_alignmentCoarseActive
                                            ? QStringLiteral("停止粗对准")
                                            : QStringLiteral("开始粗对准"));
}
```

- [ ] **Step 6: Update `src/DIMM.Alignment.cpp` lifecycle**

At start of `resetAlignmentRuntimeForStart()`:

```cpp
m_alignmentCoarseActive = false;
resetCoarseAlignmentRuntime();
```

At start of `resetAlignmentRuntimeForStop()`:

```cpp
m_alignmentCoarseActive = false;
resetCoarseAlignmentRuntime();
```

In `clearAlignmentCanvasesForStart()` and `clearAlignmentCanvasesForStop()` also call:

```cpp
clearCoarseAlignmentOverlays();
```

Implement:

```cpp
void DIMM::onToggleCoarseAlignment()
{
    if (m_captureState != CaptureState::Alignment) {
        setStatusMessage(QStringLiteral("状态: 请先进入对准模式，再启动粗对准"), UiStatusLevel::Warning);
        return;
    }

    if (m_alignmentCoarseActive) {
        m_alignmentCoarseActive = false;
        resetCoarseAlignmentRuntime();
        clearCoarseAlignmentOverlays();
        setStatusMessage(QStringLiteral("状态: 粗对准已停止，可继续自动识别或人工确认"), UiStatusLevel::Info);
        refreshActionStates();
        return;
    }

    m_alignmentCoarseActive = true;
    resetCoarseAlignmentRuntime();
    if (m_polarisSolverController) {
        m_polarisSolverController->cancelAll(m_alignmentSession.solveGeneration());
    }
    setAlignmentSolveLabel(0, QStringLiteral("粗对准: 等待星点漂移"), UiStatusLevel::Info);
    setAlignmentSolveLabel(1, QStringLiteral("粗对准: 等待星点漂移"), UiStatusLevel::Info);
    setStatusMessage(QStringLiteral("状态: 粗对准已启动，请关闭恒星跟踪并等待 15-30 秒"), UiStatusLevel::Info);
    refreshActionStates();
}
```

Implement reset/clear:

```cpp
void DIMM::resetCoarseAlignmentRuntime()
{
    m_alignmentLastCoarseSubmitMs[0] = -1;
    m_alignmentLastCoarseSubmitMs[1] = -1;
    m_alignmentCoarseEstimates[0] = CoarseAlignmentEstimate();
    m_alignmentCoarseEstimates[1] = CoarseAlignmentEstimate();
    if (m_alignmentCoarseController) {
        m_alignmentCoarseController->resetAll();
    }
}

void DIMM::clearCoarseAlignmentOverlays()
{
    if (m_fullFrameCanvas1) {
        m_fullFrameCanvas1->clearCoarseDriftOverlay();
    }
    if (m_fullFrameCanvas2) {
        m_fullFrameCanvas2->clearCoarseDriftOverlay();
    }
}
```

Build coarse config:

```cpp
CoarseAlignmentConfig DIMM::buildCoarseAlignmentConfig() const
{
    CoarseAlignmentConfig config;
    config.maxCandidates = 80;
    config.maxAssociationDistancePx = 25.0;
    config.maxStaleTrackSec = 5.0;
    config.maxTrackPoints = 90;
    config.minTrackPoints = 5;
    config.minTrackDurationSec = 15.0;
    config.minTrackDisplacementPx = 2.0;
    config.maxTrackFitRmsPx = 3.5;
    config.maxCenterResidualRmsPx = 80.0;
    config.plateScaleArcsecPx =
        206265.0 * std::max(0.001, m_alignmentPixelSizeUm / 1000.0) /
        std::max(1.0, m_alignmentFocalLengthMm);
    config.siderealArcsecSec = 15.041;
    config.minTracksForCenter = 2;
    return config;
}
```

Submit frame:

```cpp
void DIMM::submitCoarseAlignmentFrame(int cameraIndex, const CameraFrame& packet, qint64 nowMs)
{
    if (!m_alignmentCoarseController || !isValidCameraIndex(cameraIndex)) {
        return;
    }
    if (m_alignmentLastCoarseSubmitMs[cameraIndex] >= 0 &&
        nowMs - m_alignmentLastCoarseSubmitMs[cameraIndex] < m_alignmentCoarseSubmitIntervalMs) {
        return;
    }
    m_alignmentLastCoarseSubmitMs[cameraIndex] = nowMs;

    m_alignmentCoarseController->submitFrame(cameraIndex,
                                             packet.image,
                                             currentInitialStarDetectionConfig(),
                                             buildCoarseAlignmentConfig(),
                                             m_alignmentSession.solveGeneration(),
                                             packet.frameId,
                                             nowMs);
}
```

Handle result:

```cpp
void DIMM::onCoarseAlignmentEstimateReady(CoarseAlignmentEstimate estimate)
{
    if (estimate.generation != m_alignmentSession.solveGeneration() ||
        !isValidCameraIndex(estimate.cameraIndex) ||
        m_captureState != CaptureState::Alignment ||
        !m_alignmentCoarseActive) {
        return;
    }

    m_alignmentCoarseEstimates[estimate.cameraIndex] = estimate;
    updateCoarseAlignmentOverlay(estimate.cameraIndex);
    setAlignmentSolveLabel(estimate.cameraIndex,
                           estimate.statusText,
                           estimate.valid ? UiStatusLevel::Success : UiStatusLevel::Warning);
    setStatusMessage(QStringLiteral("状态: 相机%1 %2")
                         .arg(estimate.cameraIndex + 1)
                         .arg(estimate.statusText),
                     estimate.valid ? UiStatusLevel::Success : UiStatusLevel::Info);
}
```

Update overlay:

```cpp
void DIMM::updateCoarseAlignmentOverlay(int cameraIndex)
{
    FullFrameCanvas* canvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
    if (!canvas || !isValidCameraIndex(cameraIndex)) {
        return;
    }

    const CoarseAlignmentEstimate& estimate = m_alignmentCoarseEstimates[cameraIndex];
    FullFrameCanvas::CoarseDriftOverlay overlay;
    overlay.enabled = m_alignmentCoarseActive;
    overlay.valid = estimate.valid;
    overlay.northCelestialPolePx = estimate.northCelestialPolePx;
    overlay.frameCenterPx = estimate.frameCenterPx;
    overlay.adjustmentVectorPx = estimate.adjustmentVectorPx;
    overlay.offsetPx = estimate.offsetPx;
    overlay.offsetDeg = estimate.offsetDeg;
    overlay.medianSpeedPxSec = estimate.medianSpeedPxSec;
    overlay.centerResidualRmsPx = estimate.centerResidualRmsPx;
    overlay.detectedCandidateCount = estimate.detectedCandidateCount;
    overlay.usableTrackCount = estimate.usableTrackCount;
    overlay.statusText = estimate.statusText;

    for (const CoarseAlignmentTrackOverlay& track : estimate.tracks) {
        FullFrameCanvas::CoarseDriftTrackOverlay drawTrack;
        drawTrack.startPx = track.startPx;
        drawTrack.endPx = track.endPx;
        drawTrack.velocityPxSec = track.velocityPxSec;
        drawTrack.speedPxSec = track.speedPxSec;
        drawTrack.usedForSolve = track.usedForSolve;
        overlay.tracks.append(drawTrack);
    }

    canvas->setCoarseDriftOverlay(overlay);
}
```

- [ ] **Step 7: Route frames to coarse mode before automatic solve**

In `DIMM::handleAlignmentFramePacket`, after `prepareAlignmentFramePreview` succeeds and before `AlignmentFrameCoordinator::nextFrameAction`, add:

```cpp
if (m_alignmentCoarseActive) {
    submitCoarseAlignmentFrame(cameraIndex, packet, nowMs);
    finishAlignmentFramePreview(cameraIndex, packet, nowMs);
    return;
}
```

This ensures coarse drift does not trigger `PolarisSolver`.

- [ ] **Step 8: Run focused static tests**

```powershell
python -m unittest tests.test_alignment_coarse_ui_static tests.test_alignment_frame_action_static tests.test_alignment_mode_static tests.test_alignment_frame_coordinator_static
```

Expected: OK.

- [ ] **Step 9: Commit**

```powershell
git add src/DIMM.h src/DIMM.cpp src/DIMM.Ui.cpp src/DIMM.Alignment.cpp tests/test_alignment_coarse_ui_static.py
git commit -m "feat: add coarse alignment mode controls"
```

---

## Task 6: Final Static Verification

**Files:**
- No new feature files.
- Run tests only.

- [ ] **Step 1: Run all coarse-related tests**

```powershell
python -m unittest tests.test_full_frame_star_detector_config_injection_static tests.test_alignment_coarse_estimator_static tests.test_alignment_coarse_controller_static tests.test_alignment_coarse_canvas_static tests.test_alignment_coarse_ui_static
```

Expected: OK.

- [ ] **Step 2: Run regression static tests around alignment and Mono12**

```powershell
python -m unittest tests.test_full_frame_star_detection_settings_static tests.test_full_frame_star_detection_mono12_threshold_static tests.test_image_utils_extraction_static tests.test_alignment_mode_static tests.test_alignment_manual_selection_flow_static tests.test_alignment_frame_action_static tests.test_alignment_frame_coordinator_static tests.test_alignment_task_submission_split_static tests.test_polaris_star_detector_static tests.test_polaris_solver_settings_static
```

Expected: OK.

- [ ] **Step 3: Run whitespace check**

```powershell
git diff --check
```

Expected: no errors. CRLF warnings may already exist in this repo; report them without unrelated edits.

- [ ] **Step 4: Run replacement-character check**

```powershell
@'
from pathlib import Path
for rel in [
    "src/DIMM.h",
    "src/DIMM.cpp",
    "src/DIMM.Ui.cpp",
    "src/DIMM.Alignment.cpp",
    "src/CanvasWidgets.h",
    "src/CanvasWidgets.cpp",
    "src/AlignmentCoarseEstimator.h",
    "src/AlignmentCoarseEstimator.cpp",
    "src/AlignmentCoarseController.h",
    "src/AlignmentCoarseController.cpp",
]:
    text = Path(rel).read_text(encoding="utf-8-sig")
    count = text.count(chr(0xfffd))
    print(f"{rel}: replacement-char count = {count}")
    if count:
        raise SystemExit(1)
'@ | python -
```

Expected: every count is `0`.

- [ ] **Step 5: Do not build**

Stop here. Tell the user:

```text
已完成粗对准实现和静态验证。没有构建，按你的要求留给你本地构建。
```

---

## Manual Runtime Check For The User

After the user builds:

1. Enter alignment mode.
2. Click `开始粗对准`.
3. Make sure telescope sidereal tracking / guiding compensation is off.
4. Wait at least 15-30 seconds.
5. Confirm UI stays responsive.
6. Confirm each full-frame preview shows drift segments.
7. Confirm status changes from sampling to an NCP estimate.
8. Adjust the telescope so the estimated NCP approaches the frame center.
9. Stop coarse drift.
10. Click automatic recognition / retry Polaris solve.

Expected behavior:

- Coarse drift does not display catalog matched stars.
- Coarse drift does not run `PolarisSolver`.
- Full-frame star-detection settings affect candidate count on the next coarse sample.
- Very loose thresholds do not freeze the UI; at most status shows candidate overload / unstable estimate.
- If tracking is still enabled, tracks remain too short or too slow and the status should keep telling the user to wait or close tracking.
