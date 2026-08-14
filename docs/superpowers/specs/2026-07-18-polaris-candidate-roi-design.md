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

## Field Debugging Notes

Live testing showed that the most damaging failure mode was not the star detector itself, but the mixed startup/relocalization state machine around it. A visible star could be present in the full-frame preview while the ROI views stayed empty because the relocalization path had not truly returned the cameras to a fresh full-frame acquisition state.

The fixes that stopped the observed "full-frame relocalization timeout" were:

- Reset live frame acceptance gates whenever switching between ROI tracking and full-frame localization. This includes old `frameId`, accept-after timestamp, and continuous-mode frame throttle state.
- When the watchdog times out, retry the hardware full-frame switch, not just clear pending ROI candidates.
- In hardware-trigger mode, switch back to the low-rate full-frame localization trigger before searching again.
- In continuous mode, explicitly set the full-frame localization frame rate to the low-rate localization value instead of reusing the high-rate ROI tracking frame rate.
- Do not update software ROI state if hardware ROI writing fails. Software and hardware ROI must remain consistent.
- Do not use a minute/second timer to force ROI movement. ROI changes should come from valid tracking centroids or full-frame relocalization.
- Do not seed a fallback center ROI when valid centroids are missing at startup.

Two design lessons came out of this:

- The alignment-confirmed Polaris position is useful for first localization and user confirmation, but it is not a safe unconditional fallback during running relocalization.
- First localization and running relocalization should not share the same candidate-selection function. Startup can be interactive and conservative; running relocalization must be automatic, short, and tightly coupled to the hardware mode switch.

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

Do not use the same selector for running relocalization. Running relocalization should use a dedicated automatic path and should not consult the alignment-confirmed Polaris position as a general fallback. If a last tracked target position exists, it can be used only as a weak diagnostic or tie-breaker, not as a gate that prevents recovery after real telescope motion.

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
7. If ROI tracking loses the star, the app switches both cameras back to low-rate full-frame localization, resets live frame acceptance gates, clears pending relocalization candidates, and waits for fresh full-frame frames.
8. If the relocalization watchdog expires, it must repeat the hardware full-frame switch and reset frame gates again before continuing. A watchdog that only clears software state is not sufficient.
9. Once both cameras produce fresh full-frame relocalization centers, apply both hardware ROIs atomically and return to ROI tracking.

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
- If running relocalization fails to find a target before the watchdog limit, retry the full-frame hardware switch and reset frame gates before another search window.
- If only one camera has a confirmed selection, do not apply hardware ROI until the other camera is also ready.
- If a hardware ROI write fails, do not update software ROI. Report the failure and keep the previous processing ROI until hardware and software can be synchronized.

## Verification

After implementation, verify:

- A single-star full-frame still seeds ROI automatically.
- A multi-star full-frame displays all candidates and does not silently pick an ambiguous target.
- User-selected candidate produces a 64 x 64 ROI centered on that candidate.
- After ROI tracking starts, ROI images show local centroids near `(32, 32)` when centered.
- Relocalization after ROI loss resets frame gates, actually switches back to low-rate full-frame acquisition, and recovers without remaining in the timeout loop.
- A relocalization watchdog timeout causes another hardware full-frame switch attempt.
- Failed hardware ROI writes do not change the software ROI or processing ROI.
- No periodic minute-boundary rule moves ROI during tracking.
- Pair rate, dropped count, processing delay, sync jitter, and UI responsiveness remain consistent with the current stable baseline.
