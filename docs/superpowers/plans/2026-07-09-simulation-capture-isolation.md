# Simulation Capture Isolation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a dedicated simulation capture entry point that is isolated from live camera acquisition and does not touch the Galaxy SDK path.

**Architecture:** Keep the existing `CaptureState` enum, but split entry points so live capture only starts through `CameraManager` and simulation only starts through a local timer plus generated frames. Shared UI rendering stays in `DIMM`, while communication reporting is restricted to live capture only.

**Tech Stack:** Qt Widgets, Qt Actions/Timers, C++, OpenCV, existing `ImageProcessor` worker thread

---

### Task 1: Add a dedicated simulation action

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.cpp`

- [ ] Add a dedicated simulation action member and a new `onStartSimulation()` slot.
- [ ] Insert the action into the existing toolbar and tools menu after `ui->setupUi(this)`.
- [ ] Connect the new action in `setupConnections()` and add a keyboard shortcut.

### Task 2: Split live and simulation lifecycle control

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.cpp`

- [ ] Add helper methods for `stopLiveCapture()`, `stopSimulationCapture()`, and `startSimulationCapture()`.
- [ ] Update `onStartCapture()` so it only starts live camera acquisition and stops simulation first when needed.
- [ ] Update `onStopCapture()` so it shuts down both possible active paths safely before resetting UI state.

### Task 3: Generate simulation frames without camera SDK access

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.cpp`

- [ ] Add a simulation frame counter and a `buildSimulationFrame(int cameraIndex) const` helper.
- [ ] Update `onUpdateSimulation()` to build synthetic frames, refresh the preview canvas, and feed them into `ImageProcessor`.
- [ ] Keep simulation fully inside the UI/timer path and do not call `CameraManager::startAll()` or Galaxy SDK stream APIs from this flow.

### Task 4: Tighten mode-specific UI and reporting behavior

**Files:**
- Modify: `src/DIMM.cpp`

- [ ] Update `refreshActionStates()` so live capture and simulation show different button text and camera-switch availability.
- [ ] Restrict `canReportMeasurements()` so simulated data is not reported over the live communication channel.
- [ ] Verify mode switching always lands in a clean `Idle`, `Live`, or `Simulation` state instead of leaving a half-stopped mode behind.

### Task 5: Manual verification checklist

**Files:**
- No file changes required

- [ ] Launch the app and confirm `模拟采集` appears in the toolbar and tools menu.
- [ ] Start simulation with no cameras connected and confirm the app stays stable, preview updates, ROI/centroid widgets update, and no camera-connect warnings appear.
- [ ] While simulation is active, click `开始采集` and confirm simulation stops before the app checks for two connected cameras.
- [ ] With two cameras connected, start live capture and confirm the simulation timer is not running and exposure control still only applies to live frames.
