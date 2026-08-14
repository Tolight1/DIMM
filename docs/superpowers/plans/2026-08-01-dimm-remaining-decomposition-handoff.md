# DIMM Remaining Decomposition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Continue reducing `src/DIMM.cpp` after the constructor/setup, hot-pixel template, and full-frame star detector extractions already completed.

**Architecture:** Keep `DIMM` as the main orchestration class, but move member-function definitions into focused `DIMM.*.cpp` files and move shared anonymous helpers/constants into a small helper module first. Prefer mechanical moves over behavior changes. Do not introduce new controllers unless a function block is already isolated and tests prove the move.

**Tech Stack:** Qt6 C++17, OpenCV, CMake, Python `unittest` static tests. Do not run a full build unless the user explicitly allows it.

---

## Current Completed State

Already done:
- `DIMM::DIMM()` delegates to setup helpers.
- Hot-pixel threshold file parsing lives in `src/HotPixelTemplateSettings.h/.cpp`.
- Full-frame candidate detection lives in `src/FullFrameStarDetector.h/.cpp`.
- `CMakeLists.txt` already lists those helper modules.
- Relevant static tests passed with:

```powershell
python -m unittest tests.test_alignment_mode_static tests.test_polaris_candidate_roi_static tests.test_full_frame_localization_fast_static tests.test_full_frame_star_detection_mono12_threshold_static tests.test_full_frame_star_detection_settings_static tests.test_live_fullframe_locator_static tests.test_live_relocalization_state_machine_static tests.test_polaris_alignment_control_panel_static tests.test_thread_backpressure_static tests.test_polaris_solver_retry_actions_static tests.test_settings_dialog_split_static tests.test_result_writer_static tests.test_app_config_persistence_static tests.test_config_text_utils_extraction_static tests.test_periodic_auto_exposure_static.PeriodicAutoExposureStaticTest.test_startup_hot_pixel_template_settings_helper_is_extracted
```

Expected: `Ran 76 tests ... OK`.

## Hard Rules

- Do not build unless the user says to build.
- Do not use PowerShell `Get-Content` plus `Set-Content` to rewrite `src/DIMM.cpp`; it can corrupt Chinese UTF-8 text.
- If a large mechanical move is needed, use an editor or a UTF-8 aware script with `encoding='utf-8-sig'` for read and `encoding='utf-8'` for write.
- After every edit touching `src/DIMM.cpp`, run the replacement-character check below.
- Do not change behavior while moving code. If tests need updates, update only location/name assertions caused by the move.
- Do not touch unrelated dirty worktree files.

Replacement-character check:

```powershell
@'
from pathlib import Path
for rel in ["src/DIMM.cpp", "src/DIMM.h"]:
    text = Path(rel).read_text(encoding="utf-8-sig")
    count = text.count(chr(0xfffd))
    print(f"{rel}: replacement-char count = {count}")
    if count:
        raise SystemExit(1)
'@ | python -
```

Expected: both counts are `0`.

---

## Task 1: Extract Shared DIMM Runtime Helpers

**Files:**
- Create: `src/DimmRuntimeHelpers.h`
- Create: `src/DimmRuntimeHelpers.cpp`
- Modify: `src/DIMM.cpp`
- Modify: `CMakeLists.txt`
- Test: add or update `tests/test_dimm_runtime_helpers_split_static.py`

**Why:** Later split `.cpp` files need shared constants and helper functions that currently live in `DIMM.cpp` anonymous namespace.

- [ ] **Step 1: Create the static test**

Create `tests/test_dimm_runtime_helpers_split_static.py`:

```python
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class DimmRuntimeHelpersSplitStaticTest(unittest.TestCase):
    def test_runtime_helpers_are_extracted_from_dimm(self):
        dimm = read("src/DIMM.cpp")
        header = read("src/DimmRuntimeHelpers.h")
        cpp = read("src/DimmRuntimeHelpers.cpp")
        cmake = read("CMakeLists.txt")

        for token in [
            "kFixedRoiSize",
            "kSimulationFrameIntervalMs",
            "kAlignmentCandidateDetectionRefreshMs",
            "kFullFrameLocalizationPulseHz",
            "kHardwareTriggerLine",
            "kRoiUpdateGateLine",
            "kPi",
        ]:
            self.assertIn(token, header)

        for token in [
            "medianOfSamples",
            "deterministicUnitNoise",
            "decimalYearFromUtc",
            "alignRoiValue",
            "toggleButtonStyle",
            "uiStatusColor",
            "cameraStatusText",
            "cameraStatusLevel",
            "statusLabelStyle",
            "pulseConfigsMatch",
        ]:
            self.assertIn(token, header)
            self.assertIn(token, cpp)

        self.assertIn('#include "DimmRuntimeHelpers.h"', dimm)
        self.assertNotIn("QString toggleButtonStyle(bool active)", dimm)
        self.assertNotIn("bool pulseConfigsMatch(const PulseGeneratorManager::Config& lhs", dimm)
        self.assertIn("src/DimmRuntimeHelpers.h", cmake)
        self.assertIn("src/DimmRuntimeHelpers.cpp", cmake)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the new test and confirm it fails**

```powershell
python -m unittest tests.test_dimm_runtime_helpers_split_static
```

Expected: fail because the files do not exist yet.

- [ ] **Step 3: Add `src/DimmRuntimeHelpers.h`**

Use this interface:

```cpp
#pragma once

#include "CameraTypes.h"
#include "PulseGeneratorManager.h"
#include "SettingsDialog.h"

#include <QDateTime>
#include <QString>
#include <QVector>

inline constexpr int kFixedRoiSize = 64;
inline constexpr int kSimulationFrameSize = 5120;
inline constexpr int kSimulationTargetFps = 200;
inline constexpr int kSimulationFrameIntervalMs = 1000 / kSimulationTargetFps;
inline constexpr int kSimulationPreviewIntervalMs = 30000;
inline constexpr int kAlignmentPreviewIntervalMs = 1000;
inline constexpr int kAlignmentCandidateDetectionRefreshMs = 3000;
inline constexpr int kMeasurementUiIntervalMs = 100;
inline constexpr int kRoiEdgeUpdateMarginPx = 8;
inline constexpr qint64 kLostCentroidRelocalizeTimeoutMs = 1500;
inline constexpr qint64 kLiveRelocalizationMaxDurationMs = 15000;
inline constexpr double kFullFrameLocalizationPulseHz = 2.0;
inline constexpr double kAlignmentDefaultPolarisPolarDistanceArcmin = 37.6;
inline constexpr const char* kHardwareTriggerLine = "Line0";
inline constexpr const char* kRoiUpdateGateLine = "Line2";
inline constexpr double kPi = 3.14159265358979323846;

double medianOfSamples(QVector<double> samples);
double deterministicUnitNoise(int frameIndex, int salt);
double decimalYearFromUtc(const QDateTime& utcDateTime);
qint64 safeRoiIncrement(qint64 increment);
qint64 alignRoiValue(qint64 value, const RoiAxisRange& range);
QString toggleButtonStyle(bool active);
QString uiStatusColor(UiStatusLevel level);
QString cameraStatusText(bool online);
UiStatusLevel cameraStatusLevel(bool online);
QString statusLabelStyle(const QString& color);
QString statusLabelStyle(UiStatusLevel level);
bool pulseConfigsMatch(const PulseGeneratorManager::Config& lhs,
                       const PulseGeneratorManager::Config& rhs);
```

- [ ] **Step 4: Add `src/DimmRuntimeHelpers.cpp`**

Move the implementations from the top anonymous namespace of `src/DIMM.cpp`:
- `medianOfSamples`
- `deterministicUnitNoise`
- `decimalYearFromUtc`
- `safeRoiIncrement`
- `alignRoiValue`
- `toggleButtonStyle`
- `uiStatusColor`
- `cameraStatusText`
- `cameraStatusLevel`
- `statusLabelStyle` overloads
- `pulseConfigsMatch`

The new `.cpp` must include:

```cpp
#include "DimmRuntimeHelpers.h"

#include <algorithm>
#include <limits>

#include <QTime>
```

Do not move `AlignmentStartReadiness` yet; leave it in `DIMM.cpp` until alignment startup is split.

- [ ] **Step 5: Update `src/DIMM.cpp`**

Add near the other project includes:

```cpp
#include "DimmRuntimeHelpers.h"
```

Remove the moved constants and helper implementations from the anonymous namespace.

- [ ] **Step 6: Update `CMakeLists.txt`**

Inside the explicit `list(APPEND srcs ...)`, add:

```cmake
    src/DimmRuntimeHelpers.h
    src/DimmRuntimeHelpers.cpp
```

- [ ] **Step 7: Verify Task 1**

```powershell
python -m unittest tests.test_dimm_runtime_helpers_split_static
```

Expected: OK.

Also run:

```powershell
python -m unittest tests.test_settings_dialog_split_static tests.test_polaris_solver_retry_actions_static tests.test_live_fullframe_locator_static
```

Expected: OK.

Run replacement-character check and `git diff --check`.

---

## Task 2: Split Settings, Config, and Hot-Pixel Member Functions

**Files:**
- Create: `src/DIMM.Config.cpp`
- Modify: `src/DIMM.cpp`
- Modify: `CMakeLists.txt`
- Test: update `tests/test_settings_dialog_split_static.py`, `tests/test_app_config_persistence_static.py`, `tests/test_config_text_utils_extraction_static.py`, `tests/test_periodic_auto_exposure_static.py` only if they still assume these methods live in `DIMM.cpp`.

**Why:** This removes the largest configuration chunk without changing class shape.

- [ ] **Step 1: Add or update a static split test**

Create `tests/test_dimm_config_cpp_split_static.py`:

```python
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class DimmConfigCppSplitStaticTest(unittest.TestCase):
    def test_config_members_live_in_dimm_config_cpp(self):
        dimm = read("src/DIMM.cpp")
        config = read("src/DIMM.Config.cpp")
        cmake = read("CMakeLists.txt")

        for token in [
            "void DIMM::setupSettingsCallbacks()",
            "void DIMM::setupCameraSettingsCallbacks()",
            "void DIMM::setupAutoExposureSettingsCallbacks()",
            "void DIMM::setupTriggerSettingsCallbacks()",
            "void DIMM::setupEnvironmentSettingsCallbacks()",
            "void DIMM::setupPulseGeneratorSettingsCallbacks()",
            "void DIMM::setupProcessingSettingsCallbacks()",
            "void DIMM::setupOpticsSettingsCallbacks()",
            "void DIMM::setupAlignmentSettingsCallbacks()",
            "void DIMM::setupStorageSettingsCallbacks()",
            "void DIMM::setupNetworkSettingsCallbacks()",
            "AppConfig DIMM::currentAppConfig() const",
            "void DIMM::applyStartupConfig(const AppConfig& config)",
            "void DIMM::savePersistentSettings()",
            "QVector<int> DIMM::scanHotPixelExposureTemplates() const",
            "bool DIMM::applyExposureAndHotPixelTemplate(int cameraIndex",
            "void DIMM::refreshHotPixelTemplates()",
        ]:
            self.assertIn(token, config)
            self.assertNotIn(token, dimm)

        self.assertIn("src/DIMM.Config.cpp", cmake)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Create `src/DIMM.Config.cpp`**

Start with these includes:

```cpp
#include "DIMM.h"

#include "AppConfigPersistence.h"
#include "ConfigApplicationController.h"
#include "DimmRuntimeHelpers.h"
#include "HotPixelTemplateSettings.h"
#include "ImageProcessor.h"
#include "InitialStarDetectionConfig.h"
#include "PathUtils.h"
#include "SettingsDialog.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStringList>
```

- [ ] **Step 3: Move settings callback methods**

Move the whole block from `src/DIMM.cpp`:

Start marker:

```cpp
void DIMM::setupSettingsCallbacks()
```

End marker:

```cpp
void DIMM::setupCameraConnections()
```

Move everything before the end marker into `src/DIMM.Config.cpp`. Leave `setupCameraConnections()` in `DIMM.cpp`.

- [ ] **Step 4: Move app config and hot-pixel member methods**

Move these methods from the bottom of `src/DIMM.cpp` into `src/DIMM.Config.cpp`:

```cpp
AppConfig DIMM::currentAppConfig() const
void DIMM::applyStartupConfig(const AppConfig& config)
void DIMM::savePersistentSettings()
QVector<int> DIMM::scanHotPixelExposureTemplates() const
QVector<int> DIMM::scanHotPixelExposureTemplatesForCamera(int cameraIndex) const
int DIMM::selectHotPixelTemplateExposureForCurrentExposure(double currentExposure) const
int DIMM::selectHotPixelTemplateExposureForCameraExposure(int cameraIndex, double currentExposure) const
bool DIMM::resolveHotPixelTemplatePathsForExposure(...)
bool DIMM::resolveHotPixelTemplatePathsForCameraExposure(...)
bool DIMM::applyExposureAndHotPixelTemplate(int exposureUs, QString* reason)
bool DIMM::applyExposureAndHotPixelTemplate(int cameraIndex, int exposureUs, QString* reason)
void DIMM::refreshHotPixelTemplates()
```

Do not move `autoExposureStateName`, `autoExposureStateShortText`, `autoExposureUiStatusText`, or `csvSafeField` in this task.

- [ ] **Step 5: Update `CMakeLists.txt`**

Add:

```cmake
    src/DIMM.Config.cpp
```

- [ ] **Step 6: Run Task 2 tests**

```powershell
python -m unittest tests.test_dimm_config_cpp_split_static tests.test_settings_dialog_split_static tests.test_app_config_persistence_static tests.test_config_text_utils_extraction_static tests.test_periodic_auto_exposure_static
```

Expected: OK.

Run replacement-character check and `git diff --check`.

---

## Task 3: Split UI Setup and UI Refresh Member Functions

**Files:**
- Create: `src/DIMM.Ui.cpp`
- Modify: `src/DIMM.cpp`
- Modify: `CMakeLists.txt`
- Test: add `tests/test_dimm_ui_cpp_split_static.py`

**Why:** UI setup/refresh is large and mostly independent from capture logic.

- [ ] **Step 1: Add static test**

Create `tests/test_dimm_ui_cpp_split_static.py`:

```python
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class DimmUiCppSplitStaticTest(unittest.TestCase):
    def test_ui_members_live_in_dimm_ui_cpp(self):
        dimm = read("src/DIMM.cpp")
        ui_cpp = read("src/DIMM.Ui.cpp")
        cmake = read("CMakeLists.txt")

        for token in [
            "void DIMM::setupStatusBarUi()",
            "void DIMM::setupMainWindowUi()",
            "void DIMM::setupPreviewCanvases()",
            "void DIMM::setupFullFramePreviewCanvases()",
            "void DIMM::setupRoiPreviewCanvases()",
            "void DIMM::setupChartCanvases()",
            "void DIMM::setupCanvasMouseStatusConnections()",
            "void DIMM::refreshUi()",
            "void DIMM::refreshStatusUi()",
            "void DIMM::refreshCameraUi()",
            "void DIMM::refreshMeasurementUi()",
            "void DIMM::refreshPanelUi()",
            "void DIMM::refreshActionStates()",
            "void DIMM::syncCameraSelectionUi()",
            "QString DIMM::currentPreviewModeText() const",
            "void DIMM::setStatusMessage(const QString& text, const QString& color)",
            "void DIMM::setStatusMessage(const QString& text, UiStatusLevel level)",
            "void DIMM::setAlignmentSolveLabel(int cameraIndex",
            "void DIMM::setDetailViewMode(DetailViewMode mode)",
        ]:
            self.assertIn(token, ui_cpp)
            self.assertNotIn(token, dimm)

        self.assertIn("src/DIMM.Ui.cpp", cmake)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Create `src/DIMM.Ui.cpp`**

Use includes:

```cpp
#include "DIMM.h"

#include "CanvasWidgets.h"
#include "DimmRuntimeHelpers.h"
#include "FocuserControlWidget.h"
#include "ImageProcessor.h"
#include "SettingsDialog.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
```

- [ ] **Step 3: Move setup/refresh methods**

Move the methods listed in Step 1 from `src/DIMM.cpp` into `src/DIMM.Ui.cpp`.

Do not move:
- `setupRuntimeActions`
- `setupConnections`
- `setupCameraConnections`
- `setupImageProcessorConnections`

- [ ] **Step 4: Update `CMakeLists.txt`**

Add:

```cmake
    src/DIMM.Ui.cpp
```

- [ ] **Step 5: Verify Task 3**

```powershell
python -m unittest tests.test_dimm_ui_cpp_split_static tests.test_polaris_alignment_control_panel_static tests.test_measurement_ui_static tests.test_environment_sensor_integration_static
```

Expected: OK.

Run replacement-character check and `git diff --check`.

---

## Task 4: Split Live ROI and Live Relocalization Flow

**Files:**
- Create: `src/DIMM.LiveRoi.cpp`
- Modify: `src/DIMM.cpp`
- Modify: `CMakeLists.txt`
- Test: add `tests/test_dimm_live_roi_cpp_split_static.py`

**Why:** Live ROI startup/relocalization is a self-contained state machine and currently takes a large middle section of `DIMM.cpp`.

- [ ] **Step 1: Add static test**

Create `tests/test_dimm_live_roi_cpp_split_static.py`:

```python
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class DimmLiveRoiCppSplitStaticTest(unittest.TestCase):
    def test_live_roi_members_live_in_dimm_live_roi_cpp(self):
        dimm = read("src/DIMM.cpp")
        live = read("src/DIMM.LiveRoi.cpp")
        cmake = read("CMakeLists.txt")

        for token in [
            "bool DIMM::isCentroidNearCurrentRoiEdge",
            "bool DIMM::isCentroidTooFarFromCurrentRoiCenter",
            "bool DIMM::shouldUpdateRoiForRecentering",
            "void DIMM::requestLiveFullFrameRelocalization",
            "void DIMM::handleLiveRoiCentroidLoss",
            "bool DIMM::isUsableCentroidSample",
            "RoiRect DIMM::sanitizeRoi",
            "RoiRect DIMM::buildCameraCentroidRoi",
            "void DIMM::applyRoiSummary",
            "void DIMM::recordLiveRoiUpdate",
            "QString DIMM::roiRuleDescription",
            "bool DIMM::validateAndCacheLiveRoiCapabilities",
            "bool DIMM::readLivePairRoiPosition",
            "RoiRect DIMM::buildLiveCameraRoi",
            "bool DIMM::configureLiveCameras",
            "bool DIMM::applyContinuousCameraFrameRate",
            "void DIMM::advanceLiveAcquisitionGeneration",
            "void DIMM::resetLiveFrameAcceptanceGates",
            "bool DIMM::startDualCameraLocalization",
            "bool DIMM::applyLiveHardwareRois",
            "bool DIMM::applyLiveFullFrameForRelocalization",
            "bool DIMM::selectLiveRelocalizationCentroid",
            "bool DIMM::maybeSeedRoiFromFrame",
            "void DIMM::handleLiveRelocalizationWatchdog",
            "void DIMM::updateFullFrameRoiOverlay",
            "void DIMM::showDeferredLiveRelocalizationPreview",
            "void DIMM::clearPendingLiveRelocalizationRois",
            "bool DIMM::commitPairedInitialRoisIfReady",
            "bool DIMM::startHardwarePulseStage",
            "bool DIMM::startFullFrameLocalizationPulse",
            "bool DIMM::switchToRoiTrackingPulse",
            "void DIMM::updateMinuteRoi",
            "void DIMM::hideLegacyRoiScheduleUi",
        ]:
            self.assertIn(token, live)
            self.assertNotIn(token, dimm)

        self.assertIn("src/DIMM.LiveRoi.cpp", cmake)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Create `src/DIMM.LiveRoi.cpp`**

Use includes:

```cpp
#include "DIMM.h"

#include "CameraManager.h"
#include "CanvasWidgets.h"
#include "DimmRuntimeHelpers.h"
#include "FullFrameStarDetector.h"
#include "ImageProcessor.h"
#include "PathUtils.h"
#include "PolarisDetectionPipeline.h"
#include "PulseGeneratorManager.h"
#include "SettingsDialog.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QLabel>
#include <QPointF>
```

- [ ] **Step 3: Move live ROI methods**

Move exactly the methods listed in the static test from `src/DIMM.cpp` into `src/DIMM.LiveRoi.cpp`.

Do not move `onStartCapture`, `handleLiveFramePacket`, or camera signal handlers in this task.

- [ ] **Step 4: Update `CMakeLists.txt`**

Add:

```cmake
    src/DIMM.LiveRoi.cpp
```

- [ ] **Step 5: Verify Task 4**

```powershell
python -m unittest tests.test_dimm_live_roi_cpp_split_static tests.test_live_fullframe_locator_static tests.test_live_relocalization_state_machine_static tests.test_live_roi_relocalization_static tests.test_live_roi_update_rules_static tests.test_live_roi_startup_boundaries_static tests.test_result_roi_update_metadata_static tests.test_roi_recenter_settings_static
```

Expected: OK.

Run replacement-character check and `git diff --check`.

---

## Task 5: Split Alignment Flow

**Files:**
- Create: `src/DIMM.Alignment.cpp`
- Modify: `src/DIMM.cpp`
- Modify: `CMakeLists.txt`
- Test: add `tests/test_dimm_alignment_cpp_split_static.py`

**Why:** Alignment still occupies a large, conceptually separate section.

- [ ] **Step 1: Add static test**

Create `tests/test_dimm_alignment_cpp_split_static.py`:

```python
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")

class DimmAlignmentCppSplitStaticTest(unittest.TestCase):
    def test_alignment_members_live_in_dimm_alignment_cpp(self):
        dimm = read("src/DIMM.cpp")
        alignment = read("src/DIMM.Alignment.cpp")
        cmake = read("CMakeLists.txt")

        for token in [
            "void DIMM::onToggleAlignmentMode",
            "void DIMM::onConfirmCamera1PolarisCandidate",
            "void DIMM::onConfirmCamera2PolarisCandidate",
            "void DIMM::requestAlignmentPolarisSelection",
            "bool DIMM::startAlignmentMode",
            "void DIMM::stopAlignmentMode",
            "bool DIMM::prepareAlignmentCamerasForPreview",
            "void DIMM::restoreCamerasAfterAlignment",
            "void DIMM::showAlignmentModeStarted",
            "void DIMM::showAlignmentModeStopped",
            "void DIMM::resetAlignmentRuntimeForStart",
            "void DIMM::resetAlignmentRuntimeForStop",
            "void DIMM::clearAlignmentCanvasesForStart",
            "void DIMM::clearAlignmentCanvasesForStop",
            "double DIMM::fallbackAlignmentOrbitRadiusPx",
            "double DIMM::alignmentOrbitRadiusPx",
            "void DIMM::handleAlignmentFramePacket",
            "bool DIMM::handleManualAlignmentFrameTracking",
            "bool DIMM::handleAutomaticAlignmentFrameTracking",
            "bool DIMM::prepareAlignmentFramePreview",
            "void DIMM::finishAlignmentFramePreview",
            "void DIMM::requestAutomaticPolarisSolve",
            "void DIMM::requestAutomaticPolarisSolveBoth",
            "PolarisSolverConfig DIMM::buildPolarisSolverConfig",
            "void DIMM::onPolarisSolveFinished",
            "QVector<InitialStarCandidate> DIMM::collectAlignmentStarCandidates",
            "bool DIMM::handleAlignmentCandidateSelection",
            "bool DIMM::promptAlignmentCandidateSelection",
            "void DIMM::updateAlignmentOverlay",
        ]:
            self.assertIn(token, alignment)
            self.assertNotIn(token, dimm)

        self.assertIn("src/DIMM.Alignment.cpp", cmake)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Create `src/DIMM.Alignment.cpp`**

Use includes:

```cpp
#include "DIMM.h"

#include "AlignmentController.h"
#include "AlignmentFrameCoordinator.h"
#include "AlignmentLocalTracker.h"
#include "AlignmentSession.h"
#include "AlignmentTaskManager.h"
#include "AlignmentUiPresenter.h"
#include "CameraManager.h"
#include "CanvasWidgets.h"
#include "DimmRuntimeHelpers.h"
#include "FullFrameStarDetector.h"
#include "ImageUtils.h"
#include "PathUtils.h"
#include "PolarisDetectionPipeline.h"
#include "PolarisSolver.h"
#include "PolarisTracker.h"
#include "SettingsDialog.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QInputDialog>
#include <QMessageBox>
#include <QPointF>
```

- [ ] **Step 3: Move alignment methods**

Move the methods listed in the static test from `src/DIMM.cpp` into `src/DIMM.Alignment.cpp`.

Keep `AlignmentStartReadiness` in `DIMM.cpp` until `startAlignmentMode` compiles in the new file. If `validateAlignmentStartReadiness` is needed by `startAlignmentMode`, move `AlignmentStartReadiness` and `validateAlignmentStartReadiness` into `DIMM.Alignment.cpp` anonymous namespace in the same step.

- [ ] **Step 4: Update `CMakeLists.txt`**

Add:

```cmake
    src/DIMM.Alignment.cpp
```

- [ ] **Step 5: Verify Task 5**

```powershell
python -m unittest tests.test_dimm_alignment_cpp_split_static tests.test_alignment_mode_static tests.test_alignment_manual_only_static tests.test_alignment_manual_selection_flow_static tests.test_alignment_controller_split_static tests.test_alignment_flow_coordinator_split_static tests.test_polaris_candidate_roi_static tests.test_polaris_detection_pipeline_split_static tests.test_polaris_solver_dimm_integration_static tests.test_polaris_solver_retry_actions_static
```

Expected: OK.

Run replacement-character check and `git diff --check`.

---

## Task 6: Split Simulation, Result Writing, Communication, and Auto Exposure

**Files:**
- Create: `src/DIMM.Simulation.cpp`
- Create: `src/DIMM.Results.cpp`
- Create: `src/DIMM.CommCamera.cpp`
- Create: `src/DIMM.AutoExposure.cpp`
- Modify: `src/DIMM.cpp`
- Modify: `CMakeLists.txt`
- Test: add one static test file per split or one combined `tests/test_dimm_remaining_cpp_splits_static.py`.

**Recommended split:**

`src/DIMM.Simulation.cpp`:
```cpp
void DIMM::onStartSimulation()
bool DIMM::stopSimulationCapture()
bool DIMM::startSimulationCapture()
cv::Mat DIMM::buildSimulationFrame(int cameraIndex) const
void DIMM::onUpdateSimulation()
```

`src/DIMM.Results.cpp`:
```cpp
void DIMM::initResultFile()
void DIMM::initDetailResultFile()
void DIMM::closeResultFile()
void DIMM::saveResultRow(int frame)
void DIMM::saveDetailResultRows(int frame, const QVector<PairedCentroidDetail>& details)
void DIMM::flushPendingWrites()
QString DIMM::csvSafeField(QString value) const
void DIMM::reportMeasurement()
void DIMM::reportDeviceStatus()
```

`src/DIMM.CommCamera.cpp`:
```cpp
void DIMM::onConnectAll()
void DIMM::onDisconnectAll()
void DIMM::onCameraConnected(int index, QString serial, QString model)
void DIMM::onCameraDisconnected(int index)
void DIMM::onCameraError(int index, int errorCode, QString message)
void DIMM::onFrameReady(int cameraIndex)
void DIMM::onCapturedFramePacket(int cameraIndex, CameraFrame packet)
void DIMM::handleLiveFramePacket(int cameraIndex, const CameraFrame& packet)
void DIMM::scheduleHardwareTriggerStartupCheck()
void DIMM::checkHardwareTriggerStartup()
void DIMM::onCommCommand(uint8_t cmd)
```

`src/DIMM.AutoExposure.cpp`:
```cpp
void DIMM::handleAutoExposureSample(const AutoExposureFrameSample& sample)
void DIMM::resetAutoExposureState()
QString DIMM::autoExposureStateName(AutoExposureState state) const
QString DIMM::autoExposureStateShortText(AutoExposureState state) const
QString DIMM::autoExposureUiStatusText() const
```

- [ ] **Step 1: Add static test**

Create `tests/test_dimm_remaining_cpp_splits_static.py` and assert each token above is in the intended file and not in `src/DIMM.cpp`.

- [ ] **Step 2: Create each `.cpp` with `#include "DIMM.h"` and focused includes**

Start broad, then remove unused includes only if a build is allowed later. Since this plan uses static tests, do not guess too aggressively.

- [ ] **Step 3: Move one file at a time**

Move in this order:
1. `DIMM.Simulation.cpp`
2. `DIMM.Results.cpp`
3. `DIMM.AutoExposure.cpp`
4. `DIMM.CommCamera.cpp`

After each file move, run its static test and replacement-character check before moving to the next file.

- [ ] **Step 4: Update `CMakeLists.txt`**

Add:

```cmake
    src/DIMM.Simulation.cpp
    src/DIMM.Results.cpp
    src/DIMM.CommCamera.cpp
    src/DIMM.AutoExposure.cpp
```

- [ ] **Step 5: Verify Task 6**

```powershell
python -m unittest tests.test_dimm_remaining_cpp_splits_static tests.test_result_writer_static tests.test_sync_diagnostics_static tests.test_thread_backpressure_static tests.test_periodic_auto_exposure_static tests.test_acquisition_generation_static tests.test_dynamic_frame_rate_static
```

Expected: OK.

Run replacement-character check and `git diff --check`.

---

## Final Verification

Run the broad static suite used during this refactor:

```powershell
python -m unittest tests.test_alignment_mode_static tests.test_polaris_candidate_roi_static tests.test_full_frame_localization_fast_static tests.test_full_frame_star_detection_mono12_threshold_static tests.test_full_frame_star_detection_settings_static tests.test_live_fullframe_locator_static tests.test_live_relocalization_state_machine_static tests.test_polaris_alignment_control_panel_static tests.test_thread_backpressure_static tests.test_polaris_solver_retry_actions_static tests.test_settings_dialog_split_static tests.test_result_writer_static tests.test_app_config_persistence_static tests.test_config_text_utils_extraction_static tests.test_periodic_auto_exposure_static
```

Expected: all selected tests OK.

Run:

```powershell
git diff --check
```

Expected: no errors. CRLF warnings are acceptable if there are no whitespace errors.

Do not claim the C++ compiles unless a build was run with user permission.

## Stop Conditions

Stop and ask for help if:
- Any `replacement-char count` is nonzero.
- A moved member function depends on an anonymous namespace symbol not yet moved to `DimmRuntimeHelpers`.
- A static test fails for behavior, not just source-location expectation.
- `git diff --check` reports whitespace errors.
- The agent feels tempted to redesign the class instead of moving member definitions.

## Execution Order Summary

1. Extract `DimmRuntimeHelpers`.
2. Split `DIMM.Config.cpp`.
3. Split `DIMM.Ui.cpp`.
4. Split `DIMM.LiveRoi.cpp`.
5. Split `DIMM.Alignment.cpp`.
6. Split simulation/results/comm/auto-exposure into separate `.cpp` files.

Each task should be committed separately if commits are requested.
