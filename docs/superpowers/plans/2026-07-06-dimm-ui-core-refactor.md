# DIMM UI and Core Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the DIMM desktop app so the UI layout and interaction flow are clearer while camera acquisition, image processing, and communication become more robust and state-driven.

**Architecture:** Keep the existing Qt Widgets application structure, but split the oversized main-window responsibilities into clearer state/control helpers inside the DIMM module. Strengthen the data flow by making UI update from explicit runtime state instead of ad-hoc label text, and harden the camera/algorithm/communication paths with validation and consistent lifecycle handling.

**Tech Stack:** C++17, Qt6 Widgets/Network, OpenCV, Galaxy SDK, CMake

---

## File Map

- Modify: `src/DIMM.h`
- Modify: `src/DIMM.cpp`
- Modify: `src/DIMM.ui`
- Modify: `src/ImageProcessor.h`
- Modify: `src/ImageProcessor.cpp`
- Modify: `src/CommManager.h`
- Modify: `src/CommManager.cpp`
- Modify: `src/CanvasWidgets.cpp`

### Task 1: Introduce explicit main-window runtime state

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.cpp`

- [ ] **Step 1: Add runtime enums and cached UI state fields**

Add a compact state model in `src/DIMM.h` for acquisition, connection, reporting, and panel visibility so the window no longer infers state from button text.

- [ ] **Step 2: Add helper methods for state-driven UI refresh**

Implement helpers in `src/DIMM.cpp` for:
- refreshing toolbar/status labels
- refreshing camera cards
- refreshing panel visibility
- refreshing action enabled/disabled state

- [ ] **Step 3: Route existing startup through the new helpers**

Update constructor initialization in `src/DIMM.cpp` so the window ends with one centralized refresh call instead of scattered direct label updates.

- [ ] **Step 4: Run a build check**

Run: `cmake --build build --config Release`
Expected: build succeeds or reports the next compile error caused by the refactor

### Task 2: Rework the main UI layout and interaction flow

**Files:**
- Modify: `src/DIMM.ui`
- Modify: `src/DIMM.cpp`

- [ ] **Step 1: Update the `.ui` layout to reflect a clearer workflow**

Adjust the existing layout so:
- the top controls read as connect/capture/view actions
- the left panel groups device, ROI, atmosphere, and statistics more clearly
- the bottom detail area supports ROI and chart panels without mutually confusing states

- [ ] **Step 2: Replace toggle-only behavior with stable panel rules**

Update `src/DIMM.cpp` so ROI and chart areas support clearer show/hide rules and stop relying on button styles alone as state.

- [ ] **Step 3: Improve settings dialog validation and feedback**

Add validation for numeric inputs, network endpoint fields, and storage interval fields before applying settings, with message-box feedback for invalid values.

- [ ] **Step 4: Run a build check**

Run: `cmake --build build --config Release`
Expected: build succeeds or reveals UI integration issues to fix next

### Task 3: Harden acquisition lifecycle and camera status handling

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.cpp`

- [ ] **Step 1: Normalize start/pause/stop transitions**

Refactor `onStartCapture()` and `onStopCapture()` so each transition updates cached runtime state first, then applies camera/simulation behavior, then refreshes UI.

- [ ] **Step 2: Differentiate real capture, partial camera availability, and simulation**

Make start logic explicitly handle:
- both cameras available
- one camera available
- no camera available with simulation fallback

- [ ] **Step 3: Prevent unsafe operations during capture**

Disable or guard operations that should not happen mid-capture, such as risky ROI table changes or repeated connect/disconnect actions.

- [ ] **Step 4: Run a build check**

Run: `cmake --build build --config Release`
Expected: build succeeds with the new lifecycle logic

### Task 4: Tighten ROI and algorithm input handling

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.cpp`
- Modify: `src/ImageProcessor.h`
- Modify: `src/ImageProcessor.cpp`

- [ ] **Step 1: Add ROI sanitation helpers**

Clamp ROI coordinates and dimensions before they reach the image processor, and reject invalid schedule entries cleanly.

- [ ] **Step 2: Add image-processing parameter validation**

Ensure kernel size, sigma, threshold, and optical parameters cannot enter invalid or dangerous ranges.

- [ ] **Step 3: Stop using label text as the data source where possible**

Prefer cached numerical state for `r0`, `seeing`, `theta0`, `tau0`, centroid values, and use labels only as outputs.

- [ ] **Step 4: Refine atmosphere update cadence**

Reduce noisy or redundant UI updates from the processing thread and make atmosphere outputs advance on explicit valid results.

- [ ] **Step 5: Run a build check**

Run: `cmake --build build --config Release`
Expected: build succeeds after algorithm-side changes

### Task 5: Improve communication reliability and status reporting

**Files:**
- Modify: `src/DIMM.h`
- Modify: `src/DIMM.cpp`
- Modify: `src/CommManager.h`
- Modify: `src/CommManager.cpp`

- [ ] **Step 1: Separate connection state from reporting state**

Track whether the TCP link is connected independently from whether periodic reporting is active.

- [ ] **Step 2: Gate outbound reporting on valid runtime conditions**

Only send measurement frames when:
- socket is connected
- reporting is enabled
- capture/simulation is active
- a valid recent measurement exists

- [ ] **Step 3: Improve error and disconnect handling**

Make disconnects stop reporting cleanly, refresh UI state centrally, and avoid stale “connected/reporting” displays.

- [ ] **Step 4: Make default auto-connect behavior configurable**

Keep current behavior available, but route it through explicit settings/state instead of hardcoding the status side effects in multiple places.

- [ ] **Step 5: Run a build check**

Run: `cmake --build build --config Release`
Expected: build succeeds after communication refactor

### Task 6: Stabilize custom canvases for the new UI behavior

**Files:**
- Modify: `src/CanvasWidgets.cpp`
- Modify: `src/DIMM.cpp`

- [ ] **Step 1: Preserve manual zoom/pan state during image refresh**

Adjust full-frame canvas behavior so incoming frames do not unexpectedly reset interaction state unless explicitly requested.

- [ ] **Step 2: Improve empty-state and measurement overlays**

Make canvas empty states and ROI/centroid overlays align with the refactored runtime state and camera availability.

- [ ] **Step 3: Run a build check**

Run: `cmake --build build --config Release`
Expected: build succeeds after canvas adjustments

### Task 7: End-to-end verification

**Files:**
- Modify: `src/DIMM.cpp`
- Modify: `src/ImageProcessor.cpp`
- Modify: `src/CommManager.cpp`
- Modify: `src/CanvasWidgets.cpp`

- [ ] **Step 1: Review the requirements checklist against the implementation**

Verify the code now covers:
- clearer UI workflow
- unified runtime state
- safer acquisition lifecycle
- validated ROI/algorithm inputs
- safer communication/reporting behavior

- [ ] **Step 2: Run the final build verification**

Run: `cmake --build build --config Release`
Expected: exit code 0

- [ ] **Step 3: Report any residual gaps honestly**

If hardware-dependent runtime behavior cannot be fully tested locally, list the exact items needing on-device validation.
