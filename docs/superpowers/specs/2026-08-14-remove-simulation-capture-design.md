# Remove Simulation Capture Design

**Date:** 2026-08-14  
**Status:** Approved for implementation planning

## Goal

Remove the synthetic-camera acquisition feature from the application while leaving formal camera acquisition, hardware triggering, measurement processing, communication, auto exposure, ROI handling, and alignment behavior intact.

## Scope

Remove:

- `CaptureState::Simulation` and all simulation-only lifecycle methods and state.
- The synthetic-frame timer, generator, toolbar/menu action, and `Ctrl+M` shortcut.
- Simulation-specific branches in capture, UI-state, auto-acquisition, configuration, result-export messaging, and runtime selection.
- Simulation-only constants and deterministic-noise helpers.
- The `src/DIMM.Simulation.cpp` translation unit and its tracked CMake registration.
- Static tests and current user-facing README sections that describe simulation capture.

Preserve:

- The formal live-camera path through `CameraManager`, including camera startup, hardware trigger startup, frame handling, ROI updates, measurement processing, reporting, and stopping.
- Alignment mode, including its time-projected Polaris overlay. The overlay is not synthetic camera acquisition and remains functionally unchanged.
- Historical design and implementation-plan documents, even when they describe the former simulation feature.
- Existing user-owned untracked files, including `src/CMakeLists.txt`, which must not be overwritten or included in the task commit.

## Design

The application will have no synthetic acquisition state. `activeRuntime()` and `runtimeForState()` will use the single remaining live runtime context, while `hasActiveCapture()` will recognize only live and alignment modes. `onStartCapture()` will proceed directly from idle/paused/alignment guards into the existing formal-camera validation and startup path; no simulation stop or transition remains. `onStopCapture()` will retain the existing live shutdown and cleanup sequence without a simulation shutdown call.

The shared preview interval currently named `kSimulationPreviewIntervalMs` is used by the live-camera full-frame preview path. It will be renamed to a live-specific name without changing its value or timing. The simulation-only frame-size, frame-rate, frame-interval, and deterministic-noise declarations will be removed. The alignment overlay's existing `simulatedCurrentPolaris` terminology is retained because it describes a projected display marker, not acquisition, and changing it would add unnecessary risk to formal alignment code.

The UI will expose only formal capture, pause/stop, and alignment actions. Network status will use the existing live/connection text path. The export warning will refer generically to a missing acquisition result rather than instructing the user to run simulation.

## Alternatives considered

1. Keep the simulation enum and disable its action. Rejected because dead state, timers, helpers, and branches would remain in production code.
2. Remove the simulation path and simplify shared live-state code in place. Recommended because it fully removes the feature while keeping formal acquisition on its existing call path and makes accidental reintroduction visible through static guards.
3. Extract acquisition modes behind a new polymorphic interface. Rejected as an unrelated refactor that would touch formal acquisition architecture and increase regression risk.

## Validation

- Static tests must assert the simulation translation unit and capture state are absent while continuing to assert the existing live, communication, auto-exposure, ROI, and alignment split contracts.
- Run the repository's Python static test suite.
- Run the mandated Visual Studio 18 x64 Release build using `scripts/build_release_vs18.cmd`.
- Confirm the Release executable and runtime DLL checks from `CODEX_BUILD_HANDOFF.md`.
- Review `git diff` and `git status` to confirm only task files changed and all pre-existing untracked files remain.
