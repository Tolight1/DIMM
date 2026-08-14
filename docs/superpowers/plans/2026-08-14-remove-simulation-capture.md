# Remove Simulation Capture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Remove synthetic-camera acquisition and every production entry point for it while preserving formal camera acquisition and alignment behavior.

**Architecture:** Collapse the capture state machine to Idle, Live, Paused, and Alignment. Delete the synthetic-frame translation unit and route shared runtime access through the existing live runtime. Keep real-camera, trigger, ROI, measurement, communication, auto-exposure, and projected-Polaris paths unchanged except for deleting simulation-only branches and renaming one misleading live-preview constant.

**Tech Stack:** C++17, Qt 6 Widgets/Network, OpenCV, CMake, MSVC Visual Studio 18 x64, Python unittest static guards.

---

## Working rules and file map

Work on the current codex/align-agent-guidelines branch. Preserve all pre-existing untracked files. Do not edit, stage, or delete the user-owned untracked src/CMakeLists.txt; the standard build uses tracked root CMakeLists.txt.

Modify:

- CMakeLists.txt
- src/DIMM.h
- src/DIMM.cpp
- src/DIMM.Ui.cpp
- src/DIMM.Config.cpp
- src/DIMM.CommCamera.cpp
- src/DIMM.Alignment.cpp
- src/DimmRuntimeHelpers.h
- src/DimmRuntimeHelpers.cpp
- tests/test_simulation_capture_removed_static.py (new)
- tests/test_dimm_remaining_cpp_splits_static.py
- tests/test_dimm_runtime_helpers_split_static.py
- tests/test_alignment_mode_static.py
- tests/test_auto_acquisition_dimm_static.py
- tests/test_polaris_candidate_roi_static.py
- tests/test_live_roi_overlay_static.py
- README.md
- docs/README.md
- docs/README_updated.md
- docs/README_EAF.md

Delete src/DIMM.Simulation.cpp.

Do not modify historical design/implementation documents under docs/superpowers/plans/, existing historical docs/*.md plans, or existing user-owned untracked files.

### Task 1: Add the failing removal guard

Files:
- Create: tests/test_simulation_capture_removed_static.py

- [ ] Step 1: Add a static guard for the deleted feature.

Use this complete test:

~~~
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8-sig")


class SimulationCaptureRemovedStaticTest(unittest.TestCase):
    def test_simulation_translation_unit_and_cmake_registration_are_gone(self):
        self.assertFalse((ROOT / "src/DIMM.Simulation.cpp").exists())
        cmake = read("CMakeLists.txt")
        self.assertNotIn("src/DIMM.Simulation.cpp", cmake)

    def test_capture_state_and_lifecycle_have_no_simulation_path(self):
        header = read("src/DIMM.h")
        cpp = read("src/DIMM.cpp")
        ui = read("src/DIMM.Ui.cpp")
        for text in [header, cpp, ui]:
            for token in [
                "CaptureState::Simulation",
                "onStartSimulation",
                "onUpdateSimulation",
                "startSimulationCapture",
                "stopSimulationCapture",
                "m_simulationTimer",
                "m_actionStartSimulation",
                "m_simulationRuntime",
            ]:
                self.assertNotIn(token, text)


if __name__ == "__main__":
    unittest.main()
~~~

- [ ] Step 2: Run the new test and confirm the baseline failure.

Run:

~~~
python -m unittest discover -s tests -p "test_simulation_capture_removed_static.py" -q
~~~

Expected: FAIL, because the source file and simulation symbols still exist.

- [ ] Step 3: Commit only the guard.

~~~
git add -- tests/test_simulation_capture_removed_static.py
git commit -m "test: guard removal of simulation capture"
~~~

### Task 2: Remove simulation-only helpers and source registration

Files:
- Modify: src/DimmRuntimeHelpers.h
- Modify: src/DimmRuntimeHelpers.cpp
- Modify: src/DIMM.CommCamera.cpp (Task 2 also owns the live consumer rename so the tree remains compilable after the helper rename)
- Modify: CMakeLists.txt
- Delete: src/DIMM.Simulation.cpp

- [ ] Step 1: Remove simulation-only helper declarations.

In src/DimmRuntimeHelpers.h, delete:

~~~
inline constexpr int kSimulationFrameSize = 5120;
inline constexpr int kSimulationTargetFps = 200;
inline constexpr int kSimulationFrameIntervalMs = 1000 / kSimulationTargetFps;
inline constexpr double kPi = 3.14159265358979323846;
double deterministicUnitNoise(int frameIndex, int salt);
~~~

Rename the shared live preview constant without changing its value:

~~~
inline constexpr int kLiveFullFramePreviewIntervalMs = 30000;
~~~

Keep all ROI, alignment, measurement, trigger, and non-simulation helper declarations unchanged.

- [ ] Step 2: Remove the deterministic-noise implementation.

Delete deterministicUnitNoise(int frameIndex, int salt) from src/DimmRuntimeHelpers.cpp. Remove <limits> if no remaining code uses it; preserve includes required by the remaining helpers.

- [ ] Step 3: Delete the synthetic source and tracked registration.

Delete src/DIMM.Simulation.cpp. Remove only this line from tracked root CMakeLists.txt:

~~~
    src/DIMM.Simulation.cpp
~~~

Do not touch untracked src/CMakeLists.txt.

After renaming the helper constant, update the formal live full-frame preview consumer in src/DIMM.CommCamera.cpp from kSimulationPreviewIntervalMs to kLiveFullFramePreviewIntervalMs without changing the 30000 ms value or any preview logic.

- [ ] Step 4: Commit the shared cleanup.

~~~
git add -- CMakeLists.txt src/DimmRuntimeHelpers.h src/DimmRuntimeHelpers.cpp src/DIMM.Simulation.cpp
git commit -m "refactor: remove synthetic acquisition helpers"
~~~

### Task 3: Collapse the core capture state and lifecycle

Files:
- Modify: src/DIMM.h
- Modify: src/DIMM.cpp

- [ ] Step 1: Remove simulation declarations and members from src/DIMM.h.

The enum becomes:

~~~
enum class CaptureState {
    Idle,
    Live,
    Paused,
    Alignment
};
~~~

Remove private slots onStartSimulation() and onUpdateSimulation(). Remove simulationFrameIndex, lastSimulationPreviewFrame, and simulationRoiSeeded from CaptureRuntimeContext. Remove declarations for isSimulationCaptureActive(), both runtimeForState(...) overloads, stopSimulationCapture(), startSimulationCapture(), and buildSimulationFrame(...). Remove m_simulationTimer, m_actionStartSimulation, and m_simulationRuntime. Do not alter live runtime, camera, alignment, or formal timer members.

- [ ] Step 2: Remove simulation action/timer setup and shortcut wiring.

In setupRuntimeActions(), delete the block that creates m_simulationTimer and m_actionStartSimulation and inserts the action into the toolbar/menu. Leave alignment and coarse-alignment actions unchanged.

In setupConnections(), delete:

~~~
connect(m_actionStartSimulation, &QAction::triggered, this, &DIMM::onStartSimulation);
~~~

Delete only the Ctrl+M shortcut block; keep Space and Escape shortcuts.

- [ ] Step 3: Remove simulation-only centroid handling and destructor cleanup.

Delete hadBothCentroids and this complete branch from the centroid callback:

~~~
if (m_captureState == CaptureState::Simulation &&
    !runtime.simulationRoiSeeded &&
    !hadBothCentroids &&
    hasValidCentroidsForRoiUpdate()) {
    updateMinuteRoi(true);
    runtime.simulationRoiSeeded = true;
}
~~~

Keep live tracking and live ROI recentering unchanged. In DIMM::~DIMM(), delete only the m_simulationTimer stop block.

- [ ] Step 4: Simplify runtime access and mode helpers.

Replace the two activeRuntime() definitions with:

~~~
DIMM::CaptureRuntimeContext& DIMM::activeRuntime()
{
    return m_liveRuntime;
}

const DIMM::CaptureRuntimeContext& DIMM::activeRuntime() const
{
    return m_liveRuntime;
}
~~~

Delete both runtimeForState(...) definitions and isSimulationCaptureActive(). Make hasActiveCapture() return true only for Live or Alignment. Remove CaptureState::Simulation cases from captureModeName(), captureModeLabel(), and resultSubdirectoryName() while preserving every other case and fallback.

- [ ] Step 5: Remove simulation lifecycle branches without changing formal startup.

In onStartCapture(), preserve the alignment guard, live pause branch, canStartLiveCapture, camera configuration, trigger startup, reporting, and existing errors. Delete only the block beginning with:

~~~
if (m_captureState == CaptureState::Simulation) {
~~~

and ending at its updateCaptureState(CaptureState::Idle);.

In onStopCapture(), delete only:

~~~
stopSimulationCapture();
~~~

Keep live stopping, report timer handling, result closure, measurement reset, and canvas cleanup.

- [ ] Step 6: Run the core guard.

~~~
python -m unittest discover -s tests -p "test_simulation_capture_removed_static.py" -q
~~~

Expected at this intermediate stage: the guard may fail only on remaining simulation symbols in src/DIMM.Ui.cpp and src/DIMM.Config.cpp, which are explicitly reserved for Task 4. The DIMM.h/DIMM.cpp symbols handled by this task must be absent.

- [ ] Step 7: Commit the core state-machine removal.

~~~
git add -- src/DIMM.h src/DIMM.cpp
git commit -m "refactor: remove simulation capture state"
~~~

### Task 4: Remove simulation UI and secondary production branches

Files:
- Modify: src/DIMM.Ui.cpp
- Modify: src/DIMM.Config.cpp
- Modify: src/DIMM.Alignment.cpp
- Modify: src/DIMM.cpp

- [ ] Step 1: Simplify connection status UI.

In refreshStatusLabels() in src/DIMM.Ui.cpp, remove the isSimulationCaptureActive() branch. Keep the existing connected, connecting, and disconnected text branches. Append / 正在上报 whenever m_reporting is true and preserve existing status colors.

- [ ] Step 2: Remove simulation action-state and label branches.

In refreshActionStates(), delete the m_actionStartSimulation enabled block and remove m_captureState != CaptureState::Simulation from the alignment-action condition. In refreshCaptureButtonLabels(), remove every m_actionStartSimulation assignment and the CaptureState::Simulation case. In captureModeStatusText(), remove the simulation return; keep the alignment label and existing non-live fallback.

- [ ] Step 3: Remove simulation runtime cleanup and verify the live preview consumer.

In src/DIMM.Config.cpp, delete:

~~~
m_simulationRuntime.pendingPairedCentroidDetails.clear();
~~~

Confirm that src/DIMM.CommCamera.cpp uses kLiveFullFramePreviewIntervalMs for the formal live full-frame preview consumer. The rename is owned by Task 2 so this task must not duplicate the edit; do not change value, cadence, LivePreviewPolicy, frame processing, or ROI behavior.

- [ ] Step 4: Remove simulation wording from active messages only.

In src/DIMM.Alignment.cpp, change the readiness reason from “请先停止当前采集或模拟采集，再进入对准模式。” to “请先停止当前采集，再进入对准模式。” Leave projected-Polaris calculations and overlay fields unchanged.

In DIMM::onExportData() in src/DIMM.cpp, change only the missing-result warning so it asks the user to run an acquisition rather than simulation. Preserve the condition and copy logic.

- [ ] Step 5: Commit the UI and secondary cleanup.

~~~
git add -- src/DIMM.Ui.cpp src/DIMM.Config.cpp src/DIMM.Alignment.cpp src/DIMM.cpp
git commit -m "refactor: remove simulation UI branches"
~~~

### Task 5: Update static contracts and current documentation

Files:
- Modify: tests/test_dimm_remaining_cpp_splits_static.py
- Modify: tests/test_dimm_runtime_helpers_split_static.py
- Modify: tests/test_alignment_mode_static.py
- Modify: tests/test_auto_acquisition_dimm_static.py
- Modify: tests/test_polaris_candidate_roi_static.py
- Modify: tests/test_live_roi_overlay_static.py
- Modify: README.md
- Modify: docs/README.md
- Modify: docs/README_updated.md
- Modify: docs/README_EAF.md

- [ ] Step 1: Remove obsolete simulation split assertions.

Delete test_simulation_members_live_in_dimm_simulation_cpp from test_dimm_remaining_cpp_splits_static.py. Remove src/DIMM.Simulation.cpp from its CMake expected list; retain results, communication, and auto-exposure assertions.

- [ ] Step 2: Update helper expectations.

In test_dimm_runtime_helpers_split_static.py, replace kSimulationFrameIntervalMs with kLiveFullFramePreviewIntervalMs and remove deterministicUnitNoise from helper-token lists. Keep all remaining helper and CMake assertions.

- [ ] Step 3: Update source-boundary tests.

In test_alignment_mode_static.py and test_polaris_candidate_roi_static.py, change splits that end at void DIMM::onStartSimulation() to end at void DIMM::onStopCapture().

In test_auto_acquisition_dimm_static.py, replace the onStartCapture() split ending at if (m_captureState == CaptureState::Simulation) with a body split from void DIMM::onStartCapture() to void DIMM::onStopCapture(). Assert the existing live manual-stop call; add no simulation expectation.

In test_live_roi_overlay_static.py, retain the negative assertion that ROI overlay code has no preview-interval gate and add a positive communication-camera assertion for kLiveFullFramePreviewIntervalMs.

- [ ] Step 4: Remove current user-facing simulation documentation.

In README.md, docs/README.md, and docs/README_updated.md:

- Remove the feature bullet describing simulation capture.
- Remove the simulation feature-table row.
- Remove DIMM.Simulation.cpp from the source-tree listing.
- Remove the simulation start/stop validation bullet.
- Rename ### 无硬件验证 to ### 离线验证, retaining saved-image/offline checks.

In docs/README_EAF.md, change “Live、Simulation、Alignment 状态” to “Live、Alignment 状态”. Do not change historical plans/specifications.

- [ ] Step 5: Run the complete Python suite.

~~~
python -m unittest discover -s tests -p "test*.py" -q
~~~

Expected: OK with no failing tests.

- [ ] Step 6: Commit tests and current documentation.

~~~
git add -- tests/test_simulation_capture_removed_static.py tests/test_dimm_remaining_cpp_splits_static.py tests/test_dimm_runtime_helpers_split_static.py tests/test_alignment_mode_static.py tests/test_auto_acquisition_dimm_static.py tests/test_polaris_candidate_roi_static.py tests/test_live_roi_overlay_static.py README.md docs/README.md docs/README_updated.md docs/README_EAF.md
git commit -m "test: update contracts after simulation removal"
~~~

### Task 6: Verify build and protect formal acquisition

Files:
- No source changes planned; inspect the diff and build outputs.

- [ ] Step 1: Scan active code/docs for capture-specific simulation remnants.

~~~
rg -n -i "CaptureState::Simulation|onStartSimulation|onUpdateSimulation|startSimulationCapture|stopSimulationCapture|buildSimulationFrame|m_simulationTimer|m_actionStartSimulation|m_simulationRuntime|kSimulationFrame|kSimulationTargetFps|kSimulationPreviewInterval|deterministicUnitNoise|模拟采集|模拟模式" src tests CMakeLists.txt README.md docs/README.md docs/README_updated.md docs/README_EAF.md
~~~

Expected: no matches. Historical documents outside this active set may retain historical references. Alignment overlay identifiers containing simulatedCurrentPolaris are intentionally retained because they describe a projected marker, not acquisition.

- [ ] Step 2: Run the mandated Release build.

~~~
cmd.exe /d /c call scripts\build_release_vs18.cmd
~~~

Expected: exit code 0 and a newly timestamped build\Release\DIMM.exe.

- [ ] Step 3: Check required Release outputs.

~~~
Get-Item build\Release\DIMM.exe | Select-Object FullName,Length,LastWriteTime
Test-Path build\Release\Qt6Core.dll
Test-Path build\Release\GxIAPICPPEx.dll
Test-Path build\Release\opencv_world4120.dll
Test-Path build\Release\EAF_focuser.dll
~~~

Expected: the executable exists and each required runtime check returns True, subject to documented environment warnings.

- [ ] Step 4: Review final status and diff.

~~~
git status --short --branch
git diff HEAD~5..HEAD --stat
git diff HEAD~5..HEAD -- src CMakeLists.txt tests README.md docs/README.md docs/README_updated.md docs/README_EAF.md
git status --short --untracked-files=all
~~~

Confirm no pre-existing untracked file was removed or staged, and formal acquisition still contains its existing CameraManager, trigger, frame-packet, ROI, measurement, reporting, and auto-exposure code.

- [ ] Step 5: Stop at handoff until explicitly authorized to push or merge.

~~~
git status --short --branch
git log -6 --oneline --decorate
git diff master...HEAD --stat
~~~

Do not push, create a PR, or merge without explicit user approval.

### Task 7: Remove stale production wording for the deleted capture mode

Files:
- Modify: src/EafFocuserManager.cpp
- Modify: tests/test_simulation_capture_removed_static.py

- [ ] Step 1: Remove the deleted mode from the focuser safety message.

Change only the user-facing message from mentioning `capture, simulation, or alignment` to `capture or alignment`. Keep the motion gate, state checks, and error behavior unchanged.

- [ ] Step 2: Extend the removal guard to cover this active production message.

Read `src/EafFocuserManager.cpp` and assert that the deleted simulation wording is absent. Do not add a broad ban on approved alignment overlay identifiers.

- [ ] Step 3: Run the targeted guard and commit.

~~~
python -m unittest tests.test_simulation_capture_removed_static
git add -- src/EafFocuserManager.cpp tests/test_simulation_capture_removed_static.py
git commit -m "fix: remove stale simulation capture wording"
~~~

### Task 8: Remove the obsolete helper contract

Files:
- Modify: tests/test_dimm_runtime_helpers_split_static.py

- [ ] Step 1: Remove the deleted `kPi` token from the helper-header expectation.

The implementation removed `kPi` together with the synthetic-frame noise helper. Keep the live preview, ROI, alignment, trigger, and measurement helper expectations unchanged.

- [ ] Step 2: Run the focused helper contract and commit.

~~~
python -m unittest tests.test_dimm_runtime_helpers_split_static
git add -- tests/test_dimm_runtime_helpers_split_static.py
git commit -m "test: remove obsolete simulation helper contract"
~~~

## Self-review checklist

- [x] Every approved requirement has a task: code deletion, state cleanup, UI cleanup, helper cleanup, tests, current docs, and build verification.
- [x] Historical plans and user-owned untracked files are explicitly excluded.
- [x] Formal live acquisition and alignment projection are explicitly protected.
- [x] No step relies on a vague placeholder; paths, symbols, commands, and expected results are specified.
- [x] The renamed constant is consistently defined and consumed as kLiveFullFramePreviewIntervalMs.
