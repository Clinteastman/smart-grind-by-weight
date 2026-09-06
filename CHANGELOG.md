# Changelog

This file records the user-visible changes in the community-maintained release
line. Earlier release history remains available in the original project's
[GitHub releases](https://github.com/jaapp/smart-grind-by-weight/releases).

## [Unreleased]

- Serialized grind-controller updates, commands and state reads across tasks,
  preventing an in-flight update from restarting the motor after stop returns.
  Web start acknowledgements now reflect whether the controller accepted the
  start, and firmware-update task suspension waits for controller access to end.
- Finished and timed-out grinds queue their history record before notifying the
  UI. Immediate dismissal, stop or an extra time-mode pulse preserves that
  record; a full save queue is retried instead of silently losing completion.
- Live web status and settings read a coherent profile snapshot while profiles
  are edited. Active grind status reports the session's own profile.

### Motor timing

- Encode motor pulses as a continuous HIGH of the requested duration, followed
  by LOW, including longer pulses and the one-second motor test.
- Keep transmission buffers alive until stopped/completed and use the RMT
  driver's completion status instead of the GPIO level. Failed starts no longer
  report the motor as running; reset failures disable the output until reboot.
- Add host-side tests of the real driver for every supported millisecond pulse
  duration, cancellation, pending transfers and simulated driver errors.

### Bluetooth updates

- Restore the normal watchdog policy, touch input and suspended tasks after a
  failed or cancelled update. Reject empty/oversized transfers and recover
  immediately from flash write errors. If watchdog recovery fails, restart
  instead of continuing with weakened protection.

### Pulse Tune

- Cancelling Pulse Tune immediately stops the motor and closes its log before
  returning to the menu. Success and failure paths also stop any active pulse.

### Web settings

- Accept older settings forms that omit the optional panel-off controls,
  preserving the grinder's saved panel-off settings instead of rejecting the
  complete form with a missing-field error.

### Scale reliability

- Stop weight-mode grinding if no valid scale reading arrives for 500 ms,
  including during purge and settling. Show a scale-disconnected error and
  require the user to dismiss it; reconnecting does not restart the motor.
- Refuse weight-mode starts against stale readings. Time and Manual modes
  remain independent of scale availability. Failed ADC reads no longer refresh
  sample freshness using the previous reading.

## [1.5.7] - 2026-08-27

### Grinding and controls

- Fixed **Return when cup is removed** failing after an otherwise completed
  grind that finished slightly over target or reached its pulse limit.

### Display and screensaver

- Fixed the first touch after AMOLED panel wake being able to leave touch
  input suppressed until reboot. Wake-touch suppression now ends after the
  release or a bounded recovery period, and held contacts no longer keep
  resetting the display idle timer.
- Reduced false panel wakes by detecting deliberate scale changes from a short
  start-to-end weight delta instead of historical min/max noise.

## [1.5.6] - 2026-08-21

### Display and screensaver

- Separated the idle screensaver from an optional later AMOLED panel-off
  stage. The screensaver can remain visible indefinitely as before, or the
  panel can switch fully off after a configurable additional delay and wake on
  touch, scale activity or grinding. The first wake touch is consumed to avoid
  accidental controls, and panel-off remains disabled by default.

### Grinding and safety

- Fixed isolated negative load-cell readings incorrectly stopping a weight
  grind. Cup or portafilter removal is now checked against its measured
  pre-tare weight and must persist across multiple samples before stopping the
  motor.

## [1.5.5] - 2026-08-20

### Firmware updates

- Fixed V1 firmware reporting itself as V2 and consequently selecting the V2
  SH8601 image during automatic updates. Hardware identity and release-image
  selection now come from the same compile-time board definition.

### Grinding and settings

- Added a validated 30–300 ms manual motor-latency control to the touchscreen
  and web settings as a fallback when automatic Pulse Tune cannot complete.
  Values are stored on the grinder in 5 ms steps; Pulse Tune remains the
  recommended method and continues to update the same setting.

## [1.5.4] - 2026-08-16

### Grinding and settings

- Added a configurable cup/portafilter threshold for automatic profile starts,
  available on both the touchscreen and web settings page. The existing 50 g
  behaviour remains the default.
- Fixed purge amounts not surviving a restart because the former ESP32 NVS key
  exceeded the platform's 15-character limit.
- Fixed uninitialised touch state during startup and added clear HX711 ADC-rail
  diagnostics for damaged, miswired or heavily preloaded load-cell inputs.
- Refused Bluetooth firmware-update starts while the controller or motor is
  active, matching the existing Wi-Fi update safety boundary.

### Display and integrations

- Added an optional GaggiMate screensaver that reads the existing local
  GaggiMate WebSocket feed and shows readiness/profile while idle or elapsed
  time, phase, pressure, flow and temperature during a shot. It is read-only,
  uses HTTP only as a fallback and requires no GaggiMate firmware changes.
- Made browser and Home Assistant live connections tolerate short periods of
  backpressure or Wi-Fi interruption instead of briefly making every entity
  unavailable. A sustained outage still becomes unavailable normally.

### Validation

- Built V1 and V2 firmware from the native WSL filesystem and passed all four
  desktop simulator tests, including render, swipe and Manual-mode coverage.
- Flashed the combined V2 release candidate over Wi-Fi, confirmed a clean
  restart and diagnostic log, and exercised the new settings live without
  operating the motor.

## [1.5.3] - 2026-08-16

### Firmware updates

- Added a background stable-release check that runs only while the grinder is
  idle and uses the flashing site's tiny hardware-aware release manifest.
- Added a green refresh indicator when newer firmware is available. Tap it, or
  use the install button on **Menu → Wi-Fi**, to confirm and install the correct
  V1 or V2 image without opening the web interface.
- Reused the existing guarded Wi-Fi OTA path, including idle-state checks,
  temporary Bluetooth shutdown, image validation, progress display and a safe
  restart after installation.

### Home Assistant integration

- Added the versioned local-push protocol used by the separately maintained
  [Smart Grind Home Assistant integration](https://github.com/Clinteastman/smart-grind-home-assistant),
  with `_smartgrind._tcp` discovery, stable device identity and advertised
  command capabilities.
- Added controller-backed selected-profile start, manual start, stop, tare,
  dismiss-result, profile selection and weight/time mode selection. Network
  commands use the existing grinder controller and never drive the relay GPIO
  directly.
- Added optional numeric request IDs and correlated acknowledgements so clients
  cannot mistake a delayed command result for a newer request.
- Kept older API v1/browser clients compatible when they omit request IDs, and
  accepted ordinary JSON whitespace within the existing bounded text frame.

### Validation

- Built both V1 and V2 firmware targets locally and in GitHub Actions.
- Flashed the release candidate over Wi-Fi and validated discovery, status,
  live state, all 16 Home Assistant entities and correlated safe-command
  handling against physical V2 hardware running Home Assistant 2026.8.
- Completed a user-present physical motor acceptance: manual start and stop
  were both acknowledged, the motor ran for one second, and the final pushed
  state confirmed `IDLE` with the motor off.

## [1.5.2] - 2026-08-15

### Firmware updates

- Fixed one-click updates failing before the download began on
  memory-constrained devices. The grinder now fetches the exact
  hardware-specific release image from the project's GitHub Pages release
  mirror instead of following GitHub's asset redirect.
- Added explicit HTTPS connection, handshake and transfer settings, plus
  detailed TLS, clock and free-memory diagnostics for failed update
  connections.
- Verified the complete update path on V2 hardware: the grinder downloaded the
  published 2,664,000-byte V2 v1.5.1 image, validated it, flashed the inactive
  OTA partition, rebooted and rejoined Wi-Fi with its settings intact.

## [1.5.1] - 2026-08-15

### Manual grinding

- Added a target-free Manual page before Single, Double and Custom for simple
  one-tap start/stop operation.
- Made Manual mode independent of the load cell: it does not automatically
  tare, purge, predict, pulse or create a weight-history session.
- Added a large optional live-weight display and user-triggered TARE control;
  both degrade clearly when no working scale is fitted.
- Added larger Manual-page text, live elapsed time, motor-runtime accounting
  and an independent 30-second safety cutoff.

### Web controls

- Added a polished round play/stop control to the live web dashboard.
- Replaced the placeholder SG badge with a coffee-bean SVG and matching browser
  favicon.
- Added remote start for the selected Single, Double or Custom profile through
  the normal firmware grind controller, including idle-state and load-cell
  checks; the web API never drives the relay directly.
- Prevented repeated start/stop commands while the previous WebSocket command
  is awaiting acknowledgement.
- Added automatic checks for the latest stable GitHub release and a one-button
  installer that strictly selects the matching V1 or V2 application image.
- Retained local firmware-file upload as an advanced recovery and development
  option.

### Development

- Added deterministic simulator coverage for entering, running and stopping
  Manual mode while retaining the existing grind and UI performance tests.

## [1.5.0] - 2026-08-14

### Hardware and installation

- Added a dedicated firmware target for the revised Waveshare
  ESP32-S3-Touch-AMOLED-1.64 hardware, including its CO5300 display path and
  corrected GPIO assignments.
- Documented how to distinguish the controller revisions by connector layout
  and components rather than relying on the ambiguous `Rev1.1` PCB marking.
- Recorded the verified V2 motor relay connection on GPIO16 and the GPIO18
  touch-interrupt conflict.
- Added explicit V1/V2 selection to the web flasher and release packages while
  retaining USB recovery for both boards.

### Grinding and controls

- Added pause and resume while grinding by time, including correct elapsed-time
  accounting and sensor-free time-mode safety.
- Added configurable coast compensation while preserving 100% as the neutral
  default.
- Hardened grind state ordering, completion, null-sensor handling, flow maths
  and retained session logging.
- Added web profile selection for Single, Double and Custom targets without
  exposing remote motor start.

### Display and touch interface

- Improved partial display updates and LVGL buffer handling for faster,
  smoother page transitions.
- Made short swipes substantially easier to trigger on the small, recessed
  display.
- Improved settings controls and button legibility on the 280 x 456 screen.
- Added built-in and custom-image screensavers with configurable dimming,
  startup and idle timeouts.
- Applied display and screensaver settings immediately instead of requiring a
  restart.

### Wi-Fi and web interface

- Added secure first-run Wi-Fi provisioning with network scanning, captive
  portal behaviour and Improv Serial support.
- Added `smartgrind.local` mDNS discovery with the device IP shown on-screen as
  a fallback.
- Added a polished responsive local web interface with light and dark themes,
  live weight, smoothed display-only flow, grind progress and completed traces.
- Added grinder/profile settings, grind history and analytics, CSV/JSON/raw
  downloads, screensaver management and recent diagnostic logs.
- Added full-image firmware updates over Wi-Fi, including automatic idle
  Bluetooth suspension, image validation, cancellation and recovery.
- Preserved Wi-Fi credentials, profiles and display settings across firmware
  updates.

### Reliability and diagnostics

- Added automatic scale tare during startup so the first live web reading uses
  the calibrated zero point.
- Hardened Bluetooth connection, diagnostics, OTA and data-transfer lifecycle
  handling.
- Added retained startup/runtime logs that can be downloaded from the web UI.
- Improved OTA task-watchdog handling and recovery after invalid, interrupted
  or expired update attempts.

### Development and release tooling

- Added a Windows desktop simulator with deterministic grind behaviour and UI
  performance tests.
- Added independent V1/V2 CI builds, simulator CI and reproducible dual-board
  release assets.
- Added a public web flasher deployment workflow and complete hardware,
  troubleshooting, architecture and community-roadmap documentation.

### Community contributions

- Coast compensation incorporates
  [PR #106](https://github.com/jaapp/smart-grind-by-weight/pull/106) by
  [Randy1st](https://github.com/Randy1st).
- Bluetooth hardening incorporates
  [PR #111](https://github.com/jaapp/smart-grind-by-weight/pull/111) by
  [Woutifier](https://github.com/Woutifier).
- Custom screensaver support incorporates
  [PR #114](https://github.com/jaapp/smart-grind-by-weight/pull/114) by
  [quickcoffee](https://github.com/quickcoffee).
- Sensor-free time mode and pause/resume incorporate
  [PR #136](https://github.com/jaapp/smart-grind-by-weight/pull/136) by
  [FleischerT](https://github.com/FleischerT).

Original commit authorship is retained in the repository history; the release
also includes separately authored review, hardware-support and integration
fixes described above.

[1.5.0]: https://github.com/Clinteastman/smart-grind-by-weight/releases/tag/v1.5.0
[1.5.1]: https://github.com/Clinteastman/smart-grind-by-weight/releases/tag/v1.5.1
[1.5.2]: https://github.com/Clinteastman/smart-grind-by-weight/releases/tag/v1.5.2
[1.5.3]: https://github.com/Clinteastman/smart-grind-by-weight/releases/tag/v1.5.3
[1.5.4]: https://github.com/Clinteastman/smart-grind-by-weight/releases/tag/v1.5.4
[1.5.5]: https://github.com/Clinteastman/smart-grind-by-weight/releases/tag/v1.5.5
[1.5.6]: https://github.com/Clinteastman/smart-grind-by-weight/releases/tag/v1.5.6
[1.5.7]: https://github.com/Clinteastman/smart-grind-by-weight/releases/tag/v1.5.7
