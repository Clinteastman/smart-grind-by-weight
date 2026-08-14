# Changelog

This file records the user-visible changes in the community-maintained release
line. Earlier release history remains available in the original project's
[GitHub releases](https://github.com/jaapp/smart-grind-by-weight/releases).

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
