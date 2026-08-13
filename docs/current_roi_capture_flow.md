# Current ROI Capture Flow Notes

This document captures the current known-good behavior for live acquisition, ROI tracking, and why the UI is no longer freezing during high-rate capture. Use it as handoff context for a new conversation.

## Current Stable Baseline

- Main live use case: hardware trigger, long-running acquisition, typically 200-250 Hz and sometimes 300 Hz.
- ROI size is fixed at 64 x 64.
- The UI shows two full-frame preview panels side by side, one per camera.
- Full-frame preview is diagnostic only. It is intentionally low-rate and should not drive the measurement workload.
- ROI images are shown below the full-frame preview, one per camera.
- Measurement data is saved as computed parameters only. Full-frame images and ROI images are not saved.

## Why The UI Is Currently Not Freezing

The UI stays responsive because expensive or high-frequency work is not pushed directly through the GUI path.

The short version:

- Acquisition can run at 200-300 Hz, but the GUI must never try to display, copy, convert, or chart every frame.
- Camera callbacks should be treated as a fast producer path. They should enqueue or replace lightweight frame packets and return quickly.
- The GUI should be treated as a low-rate consumer path. It only receives coalesced notifications and paints the latest available state.
- ROI computation and pairing belong to the processing worker thread, while the main thread only updates visible widgets at a bounded rate.
- Full-frame preview is intentionally slow. It is for checking whether the star is still in the field, not for real-time measurement.

The important constraints are:

- Camera SDK callbacks do not do heavy processing. They hand off the latest frame packet and emit a frame-ready notification.
- `CameraManager` keeps only the latest frame packet per camera for UI delivery. It uses `frameNotificationPending` to coalesce notifications so the GUI event queue is not flooded.
- `DIMM::handleLiveFramePacket()` throttles full-frame preview updates with `lastLivePreviewUpdateMs`.
- In hardware ROI mode, incoming camera frames are already 64 x 64. The full-frame canvas must keep the last true full-frame image and only refresh the ROI overlay.
- ROI image display is throttled in `ImageProcessorWorker::emitRoiImageIfDue()` with `ROI_IMAGE_PUBLISH_INTERVAL_MS`.
- Image processing runs in `ImageProcessorWorker` on its own `QThread`, not on the main UI thread.
- Measurement UI refresh is throttled by `lastMeasurementUiUpdateMs`, so high-rate frames do not repaint labels/charts on every frame.

The practical reason the current version no longer freezes after starting capture is that it avoids these previous failure modes:

- It does not push every SDK frame into the Qt event queue as an individual UI repaint.
- It does not redraw the full-frame preview at acquisition rate.
- It does not rebuild charts or labels on every ROI frame.
- It does not perform centroid calculation, ROI validation, or pairing directly inside the GUI thread.
- It does not let full-frame preview images overwrite ROI-tracking display state after hardware ROI mode has started.
- It avoids repeated start/stop/reconfigure loops from the UI thread during normal ROI tracking.

If UI freezing returns, check these first:

- Whether a signal is emitted once per camera frame and handled by a heavy UI slot.
- Whether `QImage`/`QPixmap` conversion is happening at 200-300 Hz on the main thread.
- Whether ROI image widgets or charts are being updated for every processed frame.
- Whether full-frame preview refresh was changed from diagnostic low-rate behavior into high-rate behavior.
- Whether camera reconfiguration or pulse-board serial I/O is being triggered repeatedly while acquisition is active.
- Whether a mutex is held while emitting Qt signals or while doing image conversion.

Do not remove these throttles unless replacing them with another bounded update mechanism.

## Live Capture Startup Flow

The live hardware-trigger flow is staged:

1. User clicks the start-capture command.
2. `DIMM::onStartCapture()` resets runtime state and calls `configureLiveCameras()`.
3. In hardware trigger mode, cameras are started first with full-frame geometry.
4. `m_liveStartupPhase` enters `LocatePair`.
5. The pulse generator starts a low-frequency full-frame localization stage through `startFullFrameLocalizationPulse()`.
6. Each camera receives full-frame images. `maybeSeedRoiFromFrame()` tries to detect each star independently.
7. Once both initial ROIs are ready, `commitPairedInitialRoisIfReady()` applies the pair of hardware ROIs.
8. `applyLiveHardwareRois()` writes 64 x 64 ROI positions to both cameras.
9. The pulse generator switches to the configured high-rate ROI tracking frequency through `switchToRoiTrackingPulse()`.
10. `m_liveStartupPhase` enters `Tracking`, and `m_liveHardwareRoiActive` becomes true.

The low-frequency stage and high-frequency stage both use the pulse generator. This is why `pulseCount` is not currently a strict whole-session sample budget.

## ROI Position Logic

ROI position is per camera, not a single shared coordinate.

- Each camera has its own ROI in `ImageProcessor::m_currentRoi[2]`.
- Initial ROI is found from each camera's full-frame image.
- `buildCameraCentroidRoi(cameraIndex)` builds a 64 x 64 ROI centered on that camera's latest valid centroid.
- `sanitizeRoi()` clamps ROI to sensor bounds and fixed size.
- `buildLiveCameraRoi()` aligns ROI to camera hardware constraints.
- `applyLiveHardwareRois()` writes both camera ROIs together.

The left sidebar currently shows one summary ROI, historically camera 1. The actual processing uses independent ROIs for camera 1 and camera 2.

## Hardware ROI Update

When applying ROI during live hardware trigger:

- The code first reads current ROI positions.
- If target positions are unchanged, it returns early.
- In hardware trigger mode, cameras temporarily switch trigger source to `Line2` for ROI update gating.
- Acquisition is paused for both cameras.
- Width/height are set to fixed 64 x 64.
- Offset is moved for each camera.
- Acquisition resumes.
- Camera queues are flushed.
- Trigger source is restored to `Line0`.
- Final ROI positions are verified.

This guarded update is important. Directly moving ROI while both cameras are actively receiving high-rate trigger frames can destabilize capture.

## Frame Handling During Tracking

`DIMM::handleLiveFramePacket()` is the central live-frame entry point.

Important behavior:

- It increments runtime frame counters.
- It detects whether a frame looks like hardware ROI by checking whether frame dimensions are <= 64 x 64.
- During hardware ROI tracking, it does not replace the stored full-frame size with 64 x 64.
- During hardware ROI tracking, it does not replace the full-frame preview image with the 64 x 64 ROI frame.
- It still updates ROI overlay when preview refresh is due.
- It passes a cropped processing frame to `ImageProcessor::processFrame()`.

This distinction is critical. Treating a 64 x 64 hardware ROI frame as a full-frame image was one of the causes of bad ROI display and confusing state.

## ROI Image Processing

`ImageProcessorWorker::processFrame()` handles per-frame ROI processing:

- It reads the current ROI for that camera.
- If the incoming frame already matches the ROI window, it crops from `(0, 0)`.
- Otherwise it crops from absolute sensor ROI coordinates.
- It applies hot-pixel correction when configured.
- It calculates centroid from the corrected ROI image.
- It reports absolute centroid coordinates by adding the ROI offset back.
- It appends valid centroid samples to per-camera pending queues.
- It pairs samples using normalized frame IDs.
- It emits atmospheric parameters after enough paired samples exist, throttled to about once per second.
- It emits ROI images at a bounded display rate.

Important detail:

The displayed ROI image is the ROI frame used for processing, with visualization scaling applied by the canvas. It is not saved to disk.

## Pairing And Statistics

The UI statistics mean:

- `原始`: raw frames received from the camera side.
- `入处理`: frames actually submitted to the image processor.
- `质心`: frames with valid centroid detection.
- `配对`: successful two-camera paired differential samples.
- `丢弃`: samples removed because one side's pending centroid queue exceeded its allowed history window.
- `延迟`: image processing elapsed time, measured inside the worker.
- `同步抖动`: variation in the two-camera timestamp/frame pairing relationship.

For long stable capture, the useful health indicators are:

- Pair rate close to target frequency.
- Dropped count near zero or slowly growing.
- Processing delay stable and low.
- Sync jitter stable, not drifting upward.
- Both ROI images keep stars near the center.

## Full-Frame Preview

Full-frame preview is intentionally low-rate.

- It is used to confirm stars are still near the ROI and to show ROI overlays.
- It should not refresh at the acquisition frequency.
- Once hardware ROI tracking starts, the incoming live frames are 64 x 64 and should not overwrite the full-frame preview image.
- The preview may show an older full-frame image with a current ROI overlay. This is expected.

Changing full-frame preview from 30 s to 10 s or 5 s is possible, but it increases bandwidth and UI work. It should be tested separately from algorithm changes.

## Known Good Acquisition Results

Recent long-run checks showed:

- 250 Hz with 2000 us exposure can maintain about 249 paired samples per second.
- Dual-camera raw frame rate is about 498 frames per second total, which matches 250 Hz per camera.
- Dropped unpaired samples can remain zero in stable runs.
- Last-stage UI delay and sync jitter may become nearly constant after long operation. This is normal because the displayed values are smoothed or steady-state statistics.

## Pulse Count Caveat

The configured pulse count is not currently a strict total acquisition limit.

Reason:

- Hardware trigger startup uses a low-frequency full-frame localization pulse stage.
- After ROI is found, it starts another high-frequency ROI tracking pulse stage.
- Both stages call the pulse generator with a config containing `pulseCount`.
- The application does not currently stop acquisition when `paired_samples` reaches `pulseCount`.

Therefore, a run configured with `2000000` pulses can produce more than 2000000 paired samples if the pulse board keeps outputting or if the tracking stage restarts the count.

If a strict whole-session limit is needed, add a software-side guard:

- Record paired sample baseline at acquisition start.
- Stop capture and stop pulse output when `paired_samples - baseline >= pulseCount`.

## Things To Be Careful With

- Do not update UI labels, canvases, or charts for every camera frame.
- Do not process full-frame images at high rate unless intentionally doing localization.
- Do not overwrite full-frame preview with 64 x 64 ROI frames during hardware ROI tracking.
- Do not treat ROI-local centroid coordinates as full-frame absolute coordinates.
- Do not move hardware ROI while cameras are actively receiving `Line0` triggers without the pause/gate/resume sequence.
- Do not reset run counters during minute ROI updates or relocalization.
- Do not rely on the left sidebar single ROI summary as the full state; actual ROI is per camera.

## Centroid And ROI Change Guardrails

Future changes to centroid calculation or ROI calculation should keep these contracts unchanged unless the full pipeline is updated together.

Coordinate contracts:

- Full-frame coordinates are absolute sensor coordinates.
- Hardware ROI frames are 64 x 64 local images, but their centroid result must be converted back to absolute sensor coordinates before pairing or ROI update.
- ROI-local centroid coordinates are only for drawing markers inside the ROI image.
- The displayed text above each ROI image may show local or absolute coordinates, but the internal algorithm must be explicit about which one it uses.
- A common failure mode is accidentally using local `(x, y)` as absolute `(x, y)`, which moves the next ROI toward the sensor corner.

ROI contracts:

- ROI size is fixed at 64 x 64.
- Each camera owns an independent ROI.
- Initial ROI is seeded from full-frame localization.
- During tracking, ROI should normally follow each camera's own valid absolute centroid.
- `sanitizeRoi()` must clamp ROI inside the sensor bounds.
- `buildLiveCameraRoi()` must preserve camera hardware alignment requirements.
- Hardware ROI should be written to both cameras through the guarded pause/gate/resume path, not by ad-hoc direct writes.

Centroid quality contracts:

- Hot-pixel correction must happen before centroid detection when enabled.
- Thresholding should not be so strict that valid star cores disappear at 200-300 Hz exposure settings.
- Thresholding should not be so loose that hot pixels or background texture become the centroid target.
- A valid centroid should include enough signal pixels to reject isolated hot pixels.
- Peak brightness and signal-pixel count are useful debug values when changing centroid logic, even if they are not saved in the final parameter file.

Threading contracts:

- Do not move centroid calculation into the UI thread.
- Do not emit ROI images for every frame just to debug centroid behavior.
- Do not hold a camera mutex while doing `QImage` conversion, chart updates, serial I/O, or signal emission.
- If adding more debug counters, throttle UI display and keep detailed values in logs or saved diagnostic files instead.

## Centroid And ROI Verification Checklist

After changing centroid or ROI logic, verify in this order:

1. Full-frame localization finds one star per camera and draws ROI boxes around the actual stars.
2. After switching to 64 x 64 hardware ROI, both ROI images still contain centered stars.
3. ROI marker coordinates make sense: local coordinates should be near `(32, 32)` when centered, while absolute coordinates should be near the full-frame star location.
4. Pair rate remains close to trigger frequency: about 12000 pairs/min at 200 Hz, 15000 pairs/min at 250 Hz, and 18000 pairs/min at 300 Hz.
5. Dropped count stays near zero in stable conditions.
6. Processing delay remains stable and low.
7. Sync jitter stays stable and does not drift upward over long runs.
8. Full-frame preview still updates slowly and does not overwrite ROI-tracking display.
9. UI remains responsive during at least several minutes of live acquisition.
10. Saved measurement files still contain only computed parameters, not image data.

## Key Files

- [DIMM.cpp](../src/DIMM.cpp): UI orchestration, live startup, ROI switching, preview throttling, status updates.
- [DIMM.h](../src/DIMM.h): runtime state, startup phase, ROI helpers.
- [CameraManager.cpp](../src/CameraManager.cpp): camera open/close, streaming, hardware ROI operations, latest-frame packet handoff.
- [CameraManager.h](../src/CameraManager.h): ROI capability and camera frame structures.
- [ImageProcessor.cpp](../src/ImageProcessor.cpp): ROI crop, hot-pixel correction, centroid calculation, pairing, atmosphere calculation.
- [ImageProcessor.h](../src/ImageProcessor.h): processing worker interface and statistics signals.
- [PulseGeneratorManager.cpp](../src/PulseGeneratorManager.cpp): pulse-board register writes and start/stop behavior.

## Recommended Next Steps

1. Keep the current non-freezing data path intact.
2. If strict pulse-count capture is required, add the software-side stop guard.
3. If changing 250 Hz to 300 Hz or higher, tune exposure and validate pair rate, dropped count, delay, and sync jitter from saved measurement files.
4. If auto exposure is used, ensure the matching hot-pixel template is loaded for the active exposure.
5. If full-frame preview refresh is made faster, test UI responsiveness separately from acquisition correctness.
