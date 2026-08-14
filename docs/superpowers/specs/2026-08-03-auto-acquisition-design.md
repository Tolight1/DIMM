# Auto Acquisition Design

## Context

The DIMM application can already start and stop live acquisition through the existing main-window actions. The missing capability is unattended night operation: after the operator enables auto acquisition, the application should start live acquisition after local sunset and stop it before the next local sunrise.

The feature must also be easy to test in the lab. Lab testing should not require changing the system clock or waiting for real sunset/sunrise, so the settings include an explicit test-time override.

The stable acquisition contracts remain unchanged:

- Live acquisition still starts through the existing `DIMM::onStartCapture()` path.
- Live acquisition still stops through the existing `DIMM::onStopCapture()` path.
- Auto acquisition does not directly control cameras, ROI, pulse generation, result files, or worker queues.
- Alignment mode, simulation capture, and manual camera operations keep their current interlocks.
- Configuration is persisted through `AppConfig` and `QSettings`.

## Recommended Approach

Add an auto-acquisition configuration group and a small scheduler in the main window. The scheduler decides whether the current time is inside the configured observation window and then delegates to the existing start/stop capture actions.

The recommended schedule mode is:

```text
Start = local sunset + start offset minutes
Stop  = next local sunrise - stop offset minutes
```

For lab testing, a test-time override can replace the computed sun schedule:

```text
Start = configured test start time
Stop  = configured test stop time
```

If the stop time is earlier than the start time, the configured interval is treated as a cross-midnight interval.

## Configuration

Add `AutoAcquisitionConfig` to `AppConfig`:

- `enabled`
- `latitudeDeg`
- `longitudeDeg`
- `startOffsetMinutesAfterSunset`
- `stopOffsetMinutesBeforeSunrise`
- `testTimeOverrideEnabled`
- `testStartTime`
- `testStopTime`

Use a new `autoAcquisition/` `QSettings` group with these keys:

- `autoAcquisition/enabled`
- `autoAcquisition/latitudeDeg`
- `autoAcquisition/longitudeDeg`
- `autoAcquisition/startOffsetMinutesAfterSunset`
- `autoAcquisition/stopOffsetMinutesBeforeSunrise`
- `autoAcquisition/testTimeOverrideEnabled`
- `autoAcquisition/testStartTime`
- `autoAcquisition/testStopTime`

Validation rules:

- Latitude must be in `[-90, 90]`.
- Longitude must be in `[-180, 180]`.
- Start and stop offsets must be in `0` to `240` minutes.
- Test start and stop times must be valid local `QTime` values.

## Settings UI

Add a new "Auto Acquisition" page to `SettingsDialog`.

The page contains:

- Enable auto acquisition checkbox.
- Latitude and longitude fields.
- Start offset field: minutes after sunset.
- Stop offset field: minutes before sunrise.
- Enable test-time override checkbox.
- Test start time field.
- Test stop time field.
- Read-only preview labels for the next computed start and stop times.

The setting should be available from the current settings dialog, not as a separate top-level window. This keeps all operational configuration in one place and matches the current settings architecture.

## Scheduler

Add a scheduler method called from the existing `on1hzTick()`. Reusing the existing 1 Hz runtime tick keeps the first implementation small and avoids adding another timer lifecycle to the main window.

The scheduler should:

1. Return immediately when auto acquisition is disabled.
2. Resolve the active schedule window:
   - Use test start/stop times when test override is enabled.
   - Otherwise compute local sunset for the active observation date and local sunrise for the following morning. Before sunrise, the active observation date is the previous local date; after sunrise, it is the current local date.
3. If current time enters the active observation window and the app is idle or paused, attempt to start live acquisition through `onStartCapture()`.
4. If current time exits the observation window and live acquisition was started by the scheduler, stop through `onStopCapture()`.
5. Avoid repeated start attempts more often than a small retry interval when cameras are not ready.
6. Remember when the operator manually stops an auto-started run, and suppress auto restart for that same observation window.

The scheduler should track whether the current live capture was auto-started. It should only auto-stop captures that it started.

## Sun Times

The implementation should use an offline sun-position calculation. No network dependency is needed at runtime.

The calculation only needs sunrise and sunset times at civil accuracy for scheduler control. A NOAA-style approximation is sufficient:

- Use local date, latitude, longitude, and local time zone.
- Compute solar declination and equation of time.
- Compute local sunrise/sunset hour angles.
- Convert to local `QDateTime`.

Polar-day and polar-night edge cases should be handled explicitly, even if the expected deployment site is not polar:

- If no sunset is available for the day, the scheduler should not start and should show a warning status.
- If no sunrise is available for the next morning, the scheduler should not auto-stop from sun schedule and should show a warning status.

## Runtime Behavior

At the configured start time:

- If both cameras and required hardware are ready, live acquisition starts normally.
- If cameras are missing, the status bar reports that auto acquisition is waiting for cameras, then retries on later ticks.
- If alignment mode or simulation capture is active, auto acquisition does not take control and reports that the current mode blocks auto start.

At the configured stop time:

- If the running live capture was auto-started, stop live acquisition normally.
- Close result files through the existing stop path.
- Reset the scheduler's current-window state so the next night can start normally.

Manual operator behavior:

- If the operator manually starts live capture inside the window, auto acquisition does not mark it as auto-started.
- If the operator manually stops an auto-started run before the scheduled stop, do not restart automatically until the next observation window.
- If the operator disables and re-enables auto acquisition, clear the manual-stop suppression for the next schedule evaluation.

## Status And Logging

Status messages should be visible but not noisy:

- Auto acquisition enabled/disabled.
- Next planned start and stop time.
- Waiting for cameras or blocked by alignment/simulation mode.
- Auto-start succeeded or failed.
- Auto-stop succeeded.
- Sun-time calculation failed.

Repeated failures should be throttled to avoid showing the same warning every second.

## Error Handling

- Invalid settings are rejected in `SettingsDialog::applySettings()`.
- Missing cameras reuse `canStartLiveCapture()` and do not bypass its warnings.
- Start failures leave the scheduler armed for a later retry unless manual-stop suppression is active.
- Stop failures leave a warning status and retry on the next tick.
- Schedule calculation failures disable action for that tick but do not rewrite user settings.

## Testing

Add focused static and logic tests:

- `AppConfig` contains `AutoAcquisitionConfig`.
- `AppConfigPersistence` saves and loads every `autoAcquisition/` key.
- `SettingsDialog` exposes and validates the auto acquisition controls.
- `DIMM` applies startup config and includes auto acquisition in `currentAppConfig()`.
- Scheduler delegates start and stop through existing capture entry points.
- Cross-midnight test windows classify times correctly.
- Sun schedule mode uses sunset-plus-offset and next-sunrise-minus-offset.
- Manual stop suppression prevents same-window restart.
- Missing cameras produce retry behavior without bypassing `canStartLiveCapture()`.

Hardware validation should confirm:

- The app starts after the configured sunset offset.
- The app stops before the configured sunrise offset.
- Test-time override can start and stop a run in the lab within a few minutes.
- Manual stop does not immediately restart the run.
- Result files are opened and closed exactly as in manual live acquisition.
