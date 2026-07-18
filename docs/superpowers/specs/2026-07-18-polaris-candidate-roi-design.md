# Polaris Candidate ROI Design

## Context

During real observation, a full-frame camera image may contain more than one bright star or bright artifact. The current live startup path seeds each camera ROI from a single detected centroid. If the first full-frame localization chooses the wrong bright object, the later 64 x 64 hardware ROI tracking can remain stable but track the wrong target.

The existing stable acquisition contracts remain unchanged:

- Full-frame preview is diagnostic and low-rate.
- Hardware ROI size remains fixed at 64 x 64.
- Each camera owns an independent ROI.
- ROI-stage centroid coordinates are converted back to absolute sensor coordinates before pairing or ROI updates.
- Centroid calculation, ROI validation, and pairing stay off the UI thread.
- Hardware ROI writes continue through the guarded pause/gate/resume path.

## Reference Behavior

The reference `DualCameraSyncRoiControl.cpp` solves the multi-star startup problem by separating detection from target selection:

- Full-frame localization thresholds the image and builds 8-connected components.
- Each component becomes a star candidate with center, area, peak, bounding box, and signal.
- Invalid candidates are filtered by area and shape.
- Candidates are sorted by signal strength.
- Initial selection can be manual, fixed by candidate index, or automatic.
- Lost-star relocalization can use the last known target position as a preference and choose the nearest candidate.

## Recommended Approach

Add a candidate-list based full-frame localization path to the current project. The first implementation should target initial live ROI seeding and full-frame relocalization only. ROI high-rate centroid processing should remain unchanged.

The current functions in `src/DIMM.cpp` already contain most of the needed primitives:

- `detectInitialStarCentroid()` already uses OpenCV thresholding and connected components.
- `maybeSeedRoiFromFrame()` is the live full-frame ROI seed entry point.
- `requestLiveFullFrameRelocalization()` resets the live state when ROI tracking loses the star.

The design extends this path from "return one centroid" to "return candidates plus a selected centroid".

## Components

### InitialStarCandidate

Introduce a small local structure near the initial detection helpers:

- `index`
- `center`
- `area`
- `peak`
- `signal`
- `bbox`
- `distanceToPreference`

Candidate indices are stable within one detection result after sorting by signal descending.

### Candidate Detection

Add `detectInitialStarCandidates()` beside `detectInitialStarCentroid()`.

It should:

- Convert to Mono8 using the current logic.
- Compute background, sigma, peak, and threshold using the current `InitialStarDetectionConfig`.
- Build a binary mask.
- Run `cv::connectedComponentsWithStats()`.
- Reject candidates outside configured area limits or with overly large width/height.
- Compute candidate signal from the component pixels.
- Sort candidates by signal descending.

The existing `detectInitialStarCentroid()` can become a compatibility wrapper that selects the first candidate, so alignment mode and any existing callers keep working until they are upgraded.

### Candidate Selection

Add a small selector used by `maybeSeedRoiFromFrame()`:

- If one candidate exists, select it.
- If a preferred absolute target position exists, select the nearest candidate within a maximum distance gate.
- If multiple candidates exist during first startup and no preference exists, wait for explicit user selection or use a fixed configured index.
- If automatic fallback is enabled, select the strongest candidate and record that it was automatic.

For safety, the first UI-facing version should prefer explicit user confirmation when multiple candidates exist.

### Runtime State

Extend live runtime state with per-camera target identity hints:

- last confirmed absolute target position
- whether that target position is valid
- pending candidate list for full-frame localization
- selected initial candidate index, if user-confirmed

These values are used only during full-frame localization and relocalization. They do not change ROI-stage centroid processing.

## Data Flow

1. Live capture starts in full-frame localization.
2. `handleLiveFramePacket()` calls `maybeSeedRoiFromFrame()`.
3. `maybeSeedRoiFromFrame()` detects candidates instead of directly accepting one centroid.
4. If the target can be selected confidently, it builds a 64 x 64 ROI around the selected full-frame absolute center.
5. If both cameras have selected ROIs, `commitPairedInitialRoisIfReady()` applies both hardware ROIs through the existing guarded path.
6. During tracking, valid ROI-stage centroids update each camera's last confirmed absolute target position.
7. If ROI tracking loses the star and returns to full-frame relocalization, candidate selection uses the last confirmed absolute position as the preference.

## UI Behavior

The minimal usable UI should show candidate overlays on the existing full-frame canvases:

- candidate bounding boxes
- candidate center markers
- candidate index labels
- selected candidate highlight

If multiple candidates are found during first startup, the app should not silently choose the wrong target. It should pause before hardware ROI commit and ask the user to choose the candidate index for each camera or accept automatic strongest-candidate selection.

## Error Handling

- If no candidates are found, keep the current full-frame localization phase active.
- If candidates exist but no safe selection is possible, wait for user confirmation instead of applying ROI.
- If relocalization has a previous target but every candidate is too far from that position, keep full-frame localization active and report a warning.
- If only one camera has a confirmed selection, do not apply hardware ROI until the other camera is also ready.

## Verification

After implementation, verify:

- A single-star full-frame still seeds ROI automatically.
- A multi-star full-frame displays all candidates and does not silently pick an ambiguous target.
- User-selected candidate produces a 64 x 64 ROI centered on that candidate.
- After ROI tracking starts, ROI images show local centroids near `(32, 32)` when centered.
- Relocalization after a temporary loss chooses the candidate nearest the last absolute target, not the brightest unrelated star.
- Pair rate, dropped count, processing delay, sync jitter, and UI responsiveness remain consistent with the current stable baseline.
