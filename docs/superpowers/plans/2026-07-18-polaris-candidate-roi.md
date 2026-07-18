# Polaris Candidate ROI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make live full-frame localization choose Polaris from a candidate list instead of silently locking onto the brightest object.

**Architecture:** Keep high-rate ROI processing unchanged. Add a full-frame-only candidate detector in `DIMM.cpp`, show candidates on existing `FullFrameCanvas`, and gate initial ROI commit on either a single unambiguous candidate, a saved selected candidate index, or a last-known absolute target position during relocalization.

**Tech Stack:** Qt 6 Widgets, C++17, OpenCV connected components, existing Python static tests under `tests/`.

---

## File Structure

- Modify `src/DIMM.cpp`: add full-frame candidate structs/helpers, update `maybeSeedRoiFromFrame()`, update centroid signal handling to remember last absolute target position, and preserve existing fallback behavior for alignment mode.
- Modify `src/DIMM.h`: extend `RuntimeState` with last target position and pending candidate selection state.
- Modify `src/CanvasWidgets.h`: add candidate overlay data structures and setter/clearer methods.
- Modify `src/CanvasWidgets.cpp`: draw candidate boxes, centers, and index labels on full-frame canvases.
- Create `tests/test_polaris_candidate_roi_static.py`: static contract tests for candidate detection, candidate selection, overlay drawing, and relocalization preference.
- Run existing related tests: `tests/test_live_fullframe_locator_static.py`, `tests/test_live_roi_relocalization_static.py`, `tests/test_live_roi_overlay_static.py`, and the new test file.

## Task 1: Add Static Tests For Candidate ROI Contracts

**Files:**
- Create: `tests/test_polaris_candidate_roi_static.py`

- [ ] **Step 1: Write the failing static test**

Create `tests/test_polaris_candidate_roi_static.py` with:

```python
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


def function_body(source: str, signature: str, next_marker: str) -> str:
    return source.split(signature, 1)[1].split(next_marker, 1)[0]


class PolarisCandidateRoiStaticTest(unittest.TestCase):
    def test_initial_star_candidate_type_records_selection_metadata(self):
        dimm_cpp = read("src/DIMM.cpp")

        self.assertIn("struct InitialStarCandidate", dimm_cpp)
        for field in [
            "int index",
            "QPointF center",
            "int area",
            "double peak",
            "double signal",
            "QRect bbox",
            "double distanceToPreference",
        ]:
            self.assertIn(field, dimm_cpp)

    def test_candidate_detector_uses_connected_components_and_signal_sorting(self):
        dimm_cpp = read("src/DIMM.cpp")
        body = function_body(
            dimm_cpp,
            "QVector<InitialStarCandidate> detectInitialStarCandidates",
            "bool detectInitialStarCentroid",
        )

        self.assertIn("cv::connectedComponentsWithStats", body)
        self.assertIn("cv::CC_STAT_AREA", body)
        self.assertIn("cv::CC_STAT_WIDTH", body)
        self.assertIn("cv::CC_STAT_HEIGHT", body)
        self.assertIn("componentSignal", body)
        self.assertIn("std::sort", body)
        self.assertIn("signal > b.signal", body)

    def test_live_seed_uses_candidate_selection_before_roi_commit(self):
        dimm_cpp = read("src/DIMM.cpp")
        body = function_body(
            dimm_cpp,
            "bool DIMM::maybeSeedRoiFromFrame",
            "void DIMM::updateFullFrameRoiOverlay",
        )

        self.assertIn("detectInitialStarCandidates", body)
        self.assertIn("selectInitialStarCandidate", body)
        self.assertIn("runtime.pendingInitialCandidateSelectionRequired", body)
        self.assertLess(body.find("selectInitialStarCandidate"), body.find("commitPairedInitialRoisIfReady"))

    def test_full_frame_canvas_draws_candidate_overlay(self):
        canvas_h = read("src/CanvasWidgets.h")
        canvas_cpp = read("src/CanvasWidgets.cpp")

        self.assertIn("struct StarCandidateOverlay", canvas_h)
        self.assertIn("void setStarCandidateOverlays", canvas_h)
        self.assertIn("void clearStarCandidateOverlays", canvas_h)
        self.assertIn("drawStarCandidateOverlays", canvas_cpp)
        self.assertIn("candidate.index", canvas_cpp)
        self.assertIn("candidate.selected", canvas_cpp)

    def test_relocalization_records_and_uses_last_absolute_target(self):
        dimm_h = read("src/DIMM.h")
        dimm_cpp = read("src/DIMM.cpp")
        centroid_slot = dimm_cpp.split("&ImageProcessor::centroidReady", 1)[1].split(
            "connect(m_imageProcessor,",
            1,
        )[0]

        self.assertIn("lastTargetPosition", dimm_h)
        self.assertIn("hasLastTargetPosition", dimm_h)
        self.assertIn("runtime.lastTargetPosition[camIdx]", centroid_slot)
        self.assertIn("runtime.hasLastTargetPosition[camIdx] = true", centroid_slot)
        self.assertRegex(dimm_cpp, re.compile(r"selectInitialStarCandidate\\([^)]*hasLastTargetPosition", re.S))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the new test and verify it fails**

Run:

```powershell
python -m pytest tests/test_polaris_candidate_roi_static.py -q
```

Expected: FAIL because `InitialStarCandidate`, `detectInitialStarCandidates`, candidate overlay methods, and last target state do not exist yet.

- [ ] **Step 3: Commit the failing test**

Run:

```powershell
git add tests/test_polaris_candidate_roi_static.py
git commit -m "test: cover polaris candidate ROI contracts"
```

Expected: commit contains only the new static test.

## Task 2: Add Candidate Detection Helpers In DIMM.cpp

**Files:**
- Modify: `src/DIMM.cpp`

- [ ] **Step 1: Add candidate data structures near `InitialStarDetectionConfig`**

Insert near the existing initial localization helpers:

```cpp
struct InitialStarCandidate {
    int index = 0;
    QPointF center;
    int area = 0;
    double peak = 0.0;
    double signal = 0.0;
    QRect bbox;
    double distanceToPreference = std::numeric_limits<double>::infinity();
};

struct InitialStarSelection {
    bool selected = false;
    InitialStarCandidate candidate;
    bool requiresUserSelection = false;
    QString reason;
};
```

- [ ] **Step 2: Implement `detectInitialStarCandidates()`**

Add this function before `detectInitialStarCentroid()`:

```cpp
QVector<InitialStarCandidate> detectInitialStarCandidates(const cv::Mat& grayscale,
                                                          double* peakValue = nullptr,
                                                          double* thresholdValue = nullptr)
{
    QVector<InitialStarCandidate> candidates;
    if (grayscale.empty() || grayscale.channels() != 1) {
        return candidates;
    }

    cv::Mat mono8;
    if (grayscale.type() == CV_8UC1) {
        mono8 = grayscale;
    } else {
        grayscale.convertTo(mono8, CV_8UC1);
    }

    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(mono8, mean, stddev);

    double minValue = 0.0;
    double maxValue = 0.0;
    cv::minMaxLoc(mono8, &minValue, &maxValue);
    if (peakValue) {
        *peakValue = maxValue;
    }

    static const InitialStarDetectionConfig config = loadInitialStarDetectionConfig();
    const double dynamicThreshold = std::max({config.minimumIntensity,
                                              mean[0] + config.sigmaThreshold * stddev[0],
                                              mean[0] + (maxValue - mean[0]) * config.peakFraction});
    const double threshold = config.thresholdAbsolute >= 0.0
                                 ? config.thresholdAbsolute
                                 : dynamicThreshold;
    if (thresholdValue) {
        *thresholdValue = threshold;
    }
    if (maxValue <= threshold) {
        return candidates;
    }

    cv::Mat binary;
    cv::threshold(mono8, binary, threshold, 255.0, cv::THRESH_BINARY);

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int componentCount =
        cv::connectedComponentsWithStats(binary, labels, stats, centroids, 8, CV_32S);

    std::vector<double> componentSignal(static_cast<size_t>(componentCount), 0.0);
    std::vector<double> componentPeak(static_cast<size_t>(componentCount), 0.0);
    for (int y = 0; y < labels.rows; ++y) {
        const int* labelRow = labels.ptr<int>(y);
        const uchar* imageRow = mono8.ptr<uchar>(y);
        for (int x = 0; x < labels.cols; ++x) {
            const int label = labelRow[x];
            if (label > 0 && label < componentCount) {
                const double value = static_cast<double>(imageRow[x]);
                componentSignal[static_cast<size_t>(label)] += value;
                componentPeak[static_cast<size_t>(label)] =
                    std::max(componentPeak[static_cast<size_t>(label)], value);
            }
        }
    }

    for (int label = 1; label < componentCount; ++label) {
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        const int left = stats.at<int>(label, cv::CC_STAT_LEFT);
        const int top = stats.at<int>(label, cv::CC_STAT_TOP);
        const int width = stats.at<int>(label, cv::CC_STAT_WIDTH);
        const int height = stats.at<int>(label, cv::CC_STAT_HEIGHT);
        if (area < config.minArea || area > config.maxArea || width > 96 || height > 96) {
            continue;
        }

        InitialStarCandidate candidate;
        candidate.center = QPointF(centroids.at<double>(label, 0),
                                   centroids.at<double>(label, 1));
        candidate.area = area;
        candidate.peak = componentPeak[static_cast<size_t>(label)];
        candidate.signal = componentSignal[static_cast<size_t>(label)];
        candidate.bbox = QRect(left, top, width, height);
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

- [ ] **Step 3: Update compatibility wrapper**

At the top of `detectInitialStarCentroid()`, use candidates first:

```cpp
double detectedPeak = 0.0;
const QVector<InitialStarCandidate> candidates =
    detectInitialStarCandidates(grayscale, &detectedPeak);
if (peakValue) {
    *peakValue = detectedPeak;
}
if (!candidates.isEmpty()) {
    *centroid = candidates.first().center;
    return true;
}
```

Keep the existing fallback logic below this block so dim full-frame images still use the fast/local peak fallback.

- [ ] **Step 4: Run focused tests**

Run:

```powershell
python -m pytest tests/test_polaris_candidate_roi_static.py::PolarisCandidateRoiStaticTest::test_initial_star_candidate_type_records_selection_metadata tests/test_polaris_candidate_roi_static.py::PolarisCandidateRoiStaticTest::test_candidate_detector_uses_connected_components_and_signal_sorting tests/test_live_fullframe_locator_static.py -q
```

Expected: new candidate detector tests PASS; existing full-frame locator test still PASS.

- [ ] **Step 5: Commit candidate detection**

Run:

```powershell
git add src/DIMM.cpp
git commit -m "feat: detect initial star candidates"
```

Expected: commit contains only `src/DIMM.cpp`.

## Task 3: Add Candidate Overlay Rendering To FullFrameCanvas

**Files:**
- Modify: `src/CanvasWidgets.h`
- Modify: `src/CanvasWidgets.cpp`

- [ ] **Step 1: Add overlay API to `CanvasWidgets.h`**

Inside `FullFrameCanvas`, add:

```cpp
struct StarCandidateOverlay {
    int index = 0;
    QRectF bbox;
    QPointF center;
    bool selected = false;
};

void setStarCandidateOverlays(const QVector<StarCandidateOverlay>& candidates);
void clearStarCandidateOverlays();
```

Add private members and method:

```cpp
QVector<StarCandidateOverlay> m_starCandidateOverlays;
void drawStarCandidateOverlays(QPainter& painter);
```

- [ ] **Step 2: Implement setters in `CanvasWidgets.cpp`**

Add near the existing `setRoiList()` methods:

```cpp
void FullFrameCanvas::setStarCandidateOverlays(const QVector<StarCandidateOverlay>& candidates)
{
    m_starCandidateOverlays = candidates;
    update();
}

void FullFrameCanvas::clearStarCandidateOverlays()
{
    if (m_starCandidateOverlays.isEmpty()) {
        return;
    }
    m_starCandidateOverlays.clear();
    update();
}
```

- [ ] **Step 3: Draw candidate overlays**

Add:

```cpp
void FullFrameCanvas::drawStarCandidateOverlays(QPainter& painter)
{
    if (m_starCandidateOverlays.isEmpty() || m_image.empty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setFont(QFont("Consolas", 9, QFont::Bold));

    for (const StarCandidateOverlay& candidate : m_starCandidateOverlays) {
        const QPointF topLeft = imageToWidget(candidate.bbox.topLeft());
        const QPointF bottomRight = imageToWidget(candidate.bbox.bottomRight());
        const QRectF box(topLeft, bottomRight);
        const QPointF center = imageToWidget(candidate.center);

        const QColor color = candidate.selected ? QColor(255, 82, 82) : QColor(255, 210, 90);
        painter.setPen(QPen(color, candidate.selected ? 2.0 : 1.3));
        painter.drawRect(box);
        painter.drawLine(center + QPointF(-6.0, 0.0), center + QPointF(6.0, 0.0));
        painter.drawLine(center + QPointF(0.0, -6.0), center + QPointF(0.0, 6.0));

        painter.setPen(QColor(255, 255, 255));
        painter.drawText(box.topLeft() + QPointF(4.0, 14.0),
                         QStringLiteral("#%1").arg(candidate.index));
    }

    painter.restore();
}
```

In `paintEvent()`, call `drawStarCandidateOverlays(painter);` after `drawRoiOverlays(painter);`.

- [ ] **Step 4: Run overlay tests**

Run:

```powershell
python -m pytest tests/test_polaris_candidate_roi_static.py::PolarisCandidateRoiStaticTest::test_full_frame_canvas_draws_candidate_overlay tests/test_live_roi_overlay_static.py -q
```

Expected: PASS.

- [ ] **Step 5: Commit overlay rendering**

Run:

```powershell
git add src/CanvasWidgets.h src/CanvasWidgets.cpp
git commit -m "feat: show full-frame star candidates"
```

Expected: commit contains only canvas overlay changes.

## Task 4: Use Candidate Selection During Live ROI Seeding

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.cpp`

- [ ] **Step 1: Extend live runtime state in `DIMM.h`**

Add to `RuntimeState`:

```cpp
QPointF lastTargetPosition[2];
bool hasLastTargetPosition[2] = {false, false};
int selectedInitialCandidateIndex[2] = {-1, -1};
bool pendingInitialCandidateSelectionRequired[2] = {false, false};
```

The pending candidate list remains local to each frame for now. Do not store `InitialStarCandidate` in the header because it is a `DIMM.cpp` helper type.

- [ ] **Step 2: Add selector helper in `DIMM.cpp`**

Add near the candidate detection helpers:

```cpp
InitialStarSelection selectInitialStarCandidate(QVector<InitialStarCandidate> candidates,
                                                bool hasPreference,
                                                const QPointF& preference,
                                                int selectedCandidateIndex)
{
    InitialStarSelection selection;
    if (candidates.isEmpty()) {
        selection.reason = QStringLiteral("No initial star candidates detected");
        return selection;
    }

    if (hasPreference) {
        for (InitialStarCandidate& candidate : candidates) {
            const QPointF delta = candidate.center - preference;
            candidate.distanceToPreference = std::hypot(delta.x(), delta.y());
        }
        const auto best = std::min_element(candidates.cbegin(), candidates.cend(),
                                           [](const InitialStarCandidate& a,
                                              const InitialStarCandidate& b) {
            return a.distanceToPreference < b.distanceToPreference;
        });
        if (best != candidates.cend() && best->distanceToPreference <= 128.0) {
            selection.selected = true;
            selection.candidate = *best;
            return selection;
        }
        selection.requiresUserSelection = true;
        selection.reason = QStringLiteral("Nearest candidate is too far from the last target position");
        return selection;
    }

    if (selectedCandidateIndex > 0) {
        for (const InitialStarCandidate& candidate : candidates) {
            if (candidate.index == selectedCandidateIndex) {
                selection.selected = true;
                selection.candidate = candidate;
                return selection;
            }
        }
        selection.requiresUserSelection = true;
        selection.reason = QStringLiteral("Selected candidate index is not in the current candidate list");
        return selection;
    }

    if (candidates.size() == 1) {
        selection.selected = true;
        selection.candidate = candidates.first();
        return selection;
    }

    selection.requiresUserSelection = true;
    selection.reason = QStringLiteral("Multiple star candidates detected; confirm the Polaris candidate index");
    return selection;
}
```

- [ ] **Step 3: Add overlay mapping helper**

Add:

```cpp
QVector<FullFrameCanvas::StarCandidateOverlay> buildCandidateOverlays(
    const QVector<InitialStarCandidate>& candidates,
    int selectedIndex)
{
    QVector<FullFrameCanvas::StarCandidateOverlay> overlays;
    overlays.reserve(candidates.size());
    for (const InitialStarCandidate& candidate : candidates) {
        FullFrameCanvas::StarCandidateOverlay overlay;
        overlay.index = candidate.index;
        overlay.center = candidate.center;
        overlay.bbox = QRectF(candidate.bbox);
        overlay.selected = candidate.index == selectedIndex;
        overlays.append(overlay);
    }
    return overlays;
}
```

- [ ] **Step 4: Update `maybeSeedRoiFromFrame()`**

Replace the direct centroid detection block with:

```cpp
double peakValue = 0.0;
const QVector<InitialStarCandidate> candidates =
    detectInitialStarCandidates(mono8, &peakValue);

FullFrameCanvas* targetCanvas = cameraIndex == 0 ? m_fullFrameCanvas1 : m_fullFrameCanvas2;
if (targetCanvas) {
    targetCanvas->setStarCandidateOverlays(
        buildCandidateOverlays(candidates, runtime.selectedInitialCandidateIndex[cameraIndex]));
}

const InitialStarSelection selection =
    selectInitialStarCandidate(candidates,
                               runtime.hasLastTargetPosition[cameraIndex],
                               runtime.lastTargetPosition[cameraIndex],
                               runtime.selectedInitialCandidateIndex[cameraIndex]);
runtime.pendingInitialCandidateSelectionRequired[cameraIndex] =
    selection.requiresUserSelection;
if (!selection.selected) {
    if (selection.requiresUserSelection) {
        setStatusMessage(selection.reason, UiStatusLevel::Warning);
    }
    return false;
}

const QPointF centroid = selection.candidate.center;
```

Keep the existing ROI construction and `commitPairedInitialRoisIfReady()` call after this block.

- [ ] **Step 5: Clear candidate overlays after ROI commit**

In `commitPairedInitialRoisIfReady()`, after both `runtime.initialRoiConfirmed` values become true, add:

```cpp
if (m_fullFrameCanvas1) {
    m_fullFrameCanvas1->clearStarCandidateOverlays();
}
if (m_fullFrameCanvas2) {
    m_fullFrameCanvas2->clearStarCandidateOverlays();
}
runtime.pendingInitialCandidateSelectionRequired[0] = false;
runtime.pendingInitialCandidateSelectionRequired[1] = false;
```

- [ ] **Step 6: Run candidate selection tests**

Run:

```powershell
python -m pytest tests/test_polaris_candidate_roi_static.py::PolarisCandidateRoiStaticTest::test_live_seed_uses_candidate_selection_before_roi_commit tests/test_live_fullframe_locator_static.py tests/test_live_roi_startup_boundaries_static.py -q
```

Expected: PASS.

- [ ] **Step 7: Commit live seed selection**

Run:

```powershell
git add src/DIMM.h src/DIMM.cpp
git commit -m "feat: gate live ROI seed on star candidate selection"
```

Expected: commit contains only live seeding state and selection changes.

## Task 5: Remember Last Absolute Target For Relocalization

**Files:**
- Modify: `src/DIMM.cpp`
- Modify: `src/DIMM.h`

- [ ] **Step 1: Record last absolute centroid during tracking**

In the `ImageProcessor::centroidReady` lambda in `setupConnections()`, after `runtime.hasValidCentroid[camIdx] = true;`, add:

```cpp
if (m_captureState == CaptureState::Live && m_liveStartupPhase == LiveStartupPhase::Tracking) {
    runtime.lastTargetPosition[camIdx] = QPointF(x, y);
    runtime.hasLastTargetPosition[camIdx] = true;
}
```

These `x` and `y` values are already absolute sensor coordinates from `ImageProcessorWorker::processFrame()`.

- [ ] **Step 2: Preserve last target through full-frame relocalization reset**

In `requestLiveFullFrameRelocalization()`, keep the reset of `hasValidCentroid`, `lostCentroidFrameCount`, `initialRoiConfirmed`, and `pendingInitialRoiReady`, but do not clear `lastTargetPosition` or `hasLastTargetPosition`.

Add a comment in that function:

```cpp
// Keep lastTargetPosition across relocalization; it is the identity hint used to
// choose the nearest full-frame candidate instead of the brightest unrelated star.
```

- [ ] **Step 3: Clear target identity only on new capture state reset**

Find the runtime reset path used when a new capture begins. Add:

```cpp
runtime.hasLastTargetPosition[0] = false;
runtime.hasLastTargetPosition[1] = false;
runtime.lastTargetPosition[0] = QPointF();
runtime.lastTargetPosition[1] = QPointF();
runtime.selectedInitialCandidateIndex[0] = -1;
runtime.selectedInitialCandidateIndex[1] = -1;
runtime.pendingInitialCandidateSelectionRequired[0] = false;
runtime.pendingInitialCandidateSelectionRequired[1] = false;
```

This reset belongs at whole-run startup, not minute ROI updates or relocalization.

- [ ] **Step 4: Run relocalization tests**

Run:

```powershell
python -m pytest tests/test_polaris_candidate_roi_static.py::PolarisCandidateRoiStaticTest::test_relocalization_records_and_uses_last_absolute_target tests/test_live_roi_relocalization_static.py tests/test_roi_update_preserves_run_counters_static.py -q
```

Expected: PASS.

- [ ] **Step 5: Commit relocalization identity hint**

Run:

```powershell
git add src/DIMM.h src/DIMM.cpp
git commit -m "feat: prefer last target during live relocalization"
```

Expected: commit contains only relocalization identity changes.

## Task 6: Final Verification

**Files:**
- Test only.

- [ ] **Step 1: Run all static tests**

Run:

```powershell
python -m pytest tests -q
```

Expected: PASS for all tests.

- [ ] **Step 2: Build the application**

Run the project build command used in this workspace. If a configured build directory exists, use:

```powershell
cmake --build build --config Release
```

Expected: build succeeds with no new compiler errors.

- [ ] **Step 3: Manual hardware validation checklist**

Run live capture on hardware and verify:

- Single-star full-frame still seeds both camera ROIs automatically.
- Multi-star full-frame draws candidate boxes and does not commit ambiguous ROI silently.
- Confirmed target produces ROI images with the star near local `(32, 32)`.
- After a forced/temporary ROI loss, relocalization chooses the candidate nearest the last absolute target.
- Pair rate stays close to trigger frequency.
- Dropped count stays near zero in stable conditions.
- Processing delay and sync jitter remain stable.
- Full-frame preview remains low-rate and UI remains responsive.

- [ ] **Step 4: Commit verification notes if a doc was updated**

If manual validation notes are added to `docs/current_roi_capture_flow.md`, commit only that file:

```powershell
git add docs/current_roi_capture_flow.md
git commit -m "docs: record polaris candidate ROI validation"
```

Expected: commit is created only if the validation doc changed.

## Self-Review

- Spec coverage: candidate detection is covered by Task 2, candidate overlays by Task 3, startup selection by Task 4, relocalization preference by Task 5, and verification by Task 6.
- Scope: plan only changes full-frame localization, overlays, and relocalization identity hints. ROI-stage high-rate centroid calculation and hardware ROI pause/gate/resume remain unchanged.
- Type consistency: `InitialStarCandidate`, `InitialStarSelection`, `FullFrameCanvas::StarCandidateOverlay`, `lastTargetPosition`, and `hasLastTargetPosition` are introduced before they are used.
- Static test style matches the existing `tests/*_static.py` approach.
