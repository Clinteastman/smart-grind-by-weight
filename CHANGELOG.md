# Changelog

This file records the user-visible changes in the community-maintained release
line. Earlier release history remains available in the original project's
[GitHub releases](https://github.com/jaapp/smart-grind-by-weight/releases).

## [Unreleased]

## [1.6.6-beta.2] - 2026-10-21

### Diagnostics & Calibration
- **Diagnostics Screen Live Refresh**: Removed 1-second throttling on the Diagnostics page so standard deviation (`Std Dev (g)`, `Std Dev (ADC)`) and noise status update live at the native display/sampling rate (~80 Hz).
- **Diagnostics Submenu Scoping & Optimization**: Ensured high-frequency sensor updates only run while actively viewing the Diagnostics submenu (`is_diagnostics_page_active()`), keeping the main menu list and other submenus completely smooth. Added dirty-checking to diagnostic labels to avoid layout invalidations and keep scrolling responsive.
- **False Noise Warning Fix**: Added a noise diagnostic reset when entering Diagnostics or clicking *Reset Diagnostics*, and extended the sustained instability threshold to 3000 ms so touch gestures do not falsely trigger "Too High" noise alerts.

## [1.6.6-beta.1] - 2026-10-20

### Grinding Animation & Display
- **Expanding Disc Grind Animation**: Added a new concentric grinding disc animation with expanding grain flow and live target progress directly on the AMOLED display.
- **Customizable Color Presets**: Added 5 color schemes for the disc animation (`Emerald`, `Stealth`, `Coffee`, `Navy`, `Crimson`), selectable on the touchscreen under **Menu → Display** and in the web interface under **Settings → Display & screensaver**.
- **Interactive Animation Demo Mode**: Added a live demo button under **Menu → Display** allowing users to test and preview all grinding animations with simulated coffee flow and live weight progression without running the motor.
- **Grind Screen Completion State Fix**: Ensured the grinding screen completion state is reliably cleared after exiting demo mode so that live grinds always render the active animation.

### Menu & Navigation
- **Inactivity Auto-Return**: Added an automatic 60-second inactivity timeout in the menu (`USER_MENU_IDLE_TIMEOUT_MS = 60000`). If no touch or scale activity occurs for 60 seconds, the grinder cleanly returns to the ready screen on the active profile tab.
- **Screensaver Menu Protection**: If the screensaver activates or the display powers off while in the menu, the UI automatically returns to the ready screen first. Tapping the screen or placing a cup wakes the grinder directly ready to grind.
- **Menu Reset on Exit**: Exiting the menu (via root back chevron, idle timeout, or screensaver) resets navigation back to the top-level main page.

## [1.6.5] - 2026-10-17

### Display & Mode Icons
- **Mode Icons instead of Text**: Added toggle on the grinder touchscreen under **Menu → Display** and in the web interface under **Settings → Display & screensaver** (default enabled). When active, mode titles on the ready screen are replaced with crisp, high-resolution antialiased icons (112×112 px) rendered in pure white:
  - **Single Dose**: Open, outlined digital scale with a clear weighing platform.
  - **Single**: Minimal outlined cappuccino cup with a single steam stroke and saucer.
  - **Double**: Two outlined cups with a plus sign to make the double portion clear.
  - **Custom**: Cappuccino cup with a compact slider control badge.
- **Desktop Simulator Build Support**: Linked mode icon assets in CMake for seamless desktop simulation and CI smoke testing.

### Wi-Fi Connectivity & Signal
- **Wi-Fi High-Power Mode**: Added user-controllable toggle directly on the touchscreen under **Menu → Wi-Fi** and in the web interface under **Settings → Connectivity → Wi-Fi** (persisted in NVS as `wifi_perf`, default disabled / power-saving mode). When enabled, modem sleep is disabled and TX power is increased to 19.5 dBm for maximum throughput, low latency and robust uplink through metal grinder enclosures.
- **Real-Time Signal Display**: Added live RSSI signal indicator in dBm on the touchscreen under **Menu → Info → System Info** (and in the Wi-Fi status view) as well as under **Settings → Connectivity → Wi-Fi** in the web interface.
- **Realistic RSSI Icon Thresholds**: Re-calibrated 4-tier signal mapping on the AMOLED screen (4 bars: ≥ -67 dBm, 3 bars: -68 to -75 dBm, 2 bars: -76 to -83 dBm, 1 bar: < -83 dBm), accurately reflecting solid signal levels rather than prematurely dropping to weak indication.

## [1.6.4] - 2026-10-15

### Grinding & Workflow
- In **Prime mode** (Keep coffee) and when grounds are fresh, grinding now proceeds continuously from tare directly to target weight without intermediate motor stopping or settling pause.
- Purge pause is now cleanly restricted to **Purge mode** (Remove grinds) when grounds are stale.
- Fixed purge state tracking so confirming a purge properly records the timestamp to avoid repeated purge prompts within the freshness window.

### Screensaver & Typography
- Added custom clock font upload support (`.ttf` and `.otf`) via the web interface.
- Integrated LVGL TinyTTF runtime vector font rasterization:
  - Dynamically rasterizes uploaded vector fonts into PSRAM for both horizontal (80 px) and stacked (160 px) clock layouts.
  - Graceful fallback to built-in Montserrat-Bold font if no custom font is uploaded or if font parsing fails.
- Added strict safety boundaries:
  - 512 KB maximum font size enforced both client-side and server-side to prevent heap exhaustion or LittleFS flash saturation.
  - Magic byte header validation (`0x00010000`, `OTTO`, `true`, `typ1`) to reject invalid files before writing to flash.
- Added browser-side live font preview using the `FontFace` API when choosing a font.

## [1.6.3] - 2026-10-15

### Screensaver
- Added large clock screensaver styles (`Clock (Black)` and `Clock (Custom)`).
- Added selectable clock layouts:
  - `Horizontal (HH:MM)` using an 80 px Montserrat-Bold font with stationary colon.
  - `Stacked (HH / MM)` using a prominent 160 px Montserrat-Bold font centered for the AMOLED display.
- Added configurable clock positioning (`Center`, `Top`, `Bottom`) and browser timezone auto-detection.
- Added configurable clock colour selection with built-in presets (White, Amber, Warm Gold, Ice Cyan, Mint, Coral) and custom HEX picker with live browser preview.
- Added **Test screensaver** button in the web interface to trigger screensaver preview directly on the grinder display with touch and scale wake support.
- Integrated AMOLED burn-in protection via subtle 1–2 px subpixel shifting every minute.

### UI & Navigation Fixes
- Fixed start button erroneously showing gear icon after gesture swipe when a subset of grind tabs are disabled.
- Fixed cold boot restoring settings screen instead of the last active grind screen.

## [1.6.2] - 2026-10-14

### Fixes & Improvements

- Fixed grinder motor stopping when cancelling a grind session during weight or time modes.
- Updated unit test harnesses for single dose statistics and web configuration persistence.

## [1.6.1] - 2026-10-14

### Web Interface & Connectivity

- Subdivided the Connectivity settings page into dedicated Wi-Fi and Bluetooth sections.
- Added Web UI toggle to show or hide the Wi-Fi carousel tab on the grinder screen.
- Added Web UI toggles to hide Wi-Fi and Bluetooth status icons during grind modes.
- Instant display synchronization when connectivity settings are saved from the browser.

## [1.6.0] - 2026-10-14

### Single Dose grinding mode

- Unified Single Dose workflow into the `GrindMode::MANUAL` mode enum, removing separate mode ambiguities while maintaining dedicated profile parameters.
- Added configurable Single Dose safety cutoff limit (between 60 s and 300 s) in web settings and device menu to prevent unattended motor overruns.
- Added lifetime and session statistics tracking specifically for single dose grinds.

### Wi-Fi management & stability

- Added on-device Wi-Fi toggle in the settings menu, enabling complete hardware power-down of the Wi-Fi radio to reduce power consumption and eliminate RF interference.
- Fixed Wi-Fi reconnection stability and `AsyncWebServer` lifecycle management, ensuring reliable reconnection without requiring a grinder reboot.
- Added configurable Wi-Fi carousel tab toggle in the settings menu to show or hide the network status screen from the main carousel.
- Added option to hide the Wi-Fi status indicator icon during grinding modes, showing it only when navigating menus.

### Bluetooth icon visibility

- Added option to hide the Bluetooth status indicator icon during active grinding modes, displaying it only when in the menu screen.

## [1.5.9] - 2026-09-06

### Firmware update hotfix

- Wait for the first downloaded firmware byte before checking the image header.
  A delayed HTTPS response body no longer incorrectly fails at 0% with
  "Download is not an ESP32 application image".
- Keep the wait bounded and handle disconnection and timer rollover. Invalid
  images are still rejected before opening the inactive firmware partition.
- Show failed updates in the web interface and confirm the installed version
  after restart. Keep update buttons locked during preparation and dispatch.
- If automatic installation fails on older firmware, download the matching V1
  or V2 application `.bin` from this release and use **System & updates → Manual
  firmware file → Upload firmware** once. This works over Wi-Fi and retains
  settings; subsequent automatic updates use the repaired downloader.

## [1.5.8] - 2026-09-06

### Pulse Tune

- Cancelling Pulse Tune immediately stops the motor and closes its log before
  returning to the menu. Success and failure paths also stop any active pulse.
- Pulse Tune reports failure if saving the measured latency fails, retaining
  the previous active value. Manual latency and coast controls also restore
  their active values instead of displaying a rejected change.

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

### Scale reliability

- Stop weight-mode grinding if no valid scale reading arrives for 500 ms,
  including during purge and settling. Show a scale-disconnected error and
  require the user to dismiss it; reconnecting does not restart the motor.
- Refuse weight-mode starts against stale readings. Time and Manual modes
  remain independent of scale availability. Failed ADC reads no longer refresh
  sample freshness using the previous reading.
- Reject ADC rail readings and incomplete HX711 conversions before filtering,
  taring or refreshing sample freshness, while keeping raw fault diagnostics.

### Web settings

- Accept older settings forms that omit the optional panel-off controls,
  preserving the grinder's saved panel-off settings instead of rejecting the
  complete form with a missing-field error.
- Wait for checked persistence and touchscreen runtime refresh before reporting
  a settings save as successful. Show pending, busy, partial-save and unconfirmed
  outcomes; restore the form after failures without automatically resubmitting.
- Resolve omitted panel-off values when applying the form so an older queued
  form cannot overwrite settings saved by a newer form.

### Controller and history

- Serialized grind-controller updates, commands and state reads across tasks,
  preventing an in-flight update from restarting the motor after stop returns.
  Web start acknowledgements now reflect whether the controller accepted the
  start, and firmware-update task suspension waits for controller access to end.
- Finished and timed-out grinds queue their history record before notifying the
  UI. Immediate dismissal, stop or an extra time-mode pulse preserves that
  record; a full save queue is retried instead of silently losing completion.
- Record the actual completion timestamp, not the later file-write time.
- Reserve one shared operation slot for grinding, tuning, motor tests, settings
  application and firmware updates, retaining ownership through cleanup and
  rejecting stale callbacks from previous operations.
- Live web status and settings read a coherent profile snapshot while profiles
  are edited. Active grind status reports the session's own profile.
- Hardened grind history against allocation failures, incomplete session files
  and malformed filenames. Bluetooth export no longer hangs when its file list
  contains no usable sessions, and retention sorts only verified file IDs.
- Failed history writes are now reported as not saved. Removed unused legacy
  logging/export code; existing schema-2 files keep their unchanged layout and
  zero reserved checksum field (no new checksum or data migration).

### Touchscreen updates

- Fixed touchscreen state updates being lost behind a backlog of live weight
  readings. The screen now catches up to the latest grind phase or final result,
  retains error text safely and acknowledges a rapid stop/restart correctly.

### Development

- Run simulator checks for controller and system changes as well as UI changes.
  Standalone policy tests also run on Linux, independently of the desktop
  rendering test.

### Wi-Fi and web interface

- Serve the embedded setup and grinder pages directly from flash instead of
  copying the full page into scarce internal RAM when a browser connects.
  This removes an avoidable roughly 50 KB allocation when opening the web UI.
- Prevent browsers retaining an outdated settings page across firmware updates.

### Validation

- Both firmware targets and all 26 host regression tests pass for the reviewed
  candidate. V2 Wi-Fi installation retained the exposed settings; a supervised
  18 g grind finished at 18.03 g and saved its history. A display-setting change
  and restoration returned confirmed runtime results without rebooting.
- V1 physical feedback remains welcome. Fault-injection tests are host coverage,
  not a claim of physical disconnected-sensor or interrupted-OTA testing.

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
[1.5.8]: https://github.com/Clinteastman/smart-grind-by-weight/releases/tag/v1.5.8
[1.5.9]: https://github.com/Clinteastman/smart-grind-by-weight/releases/tag/v1.5.9
[1.6.0]: https://github.com/Cuxx/smart-grind-by-weight-singledose/releases/tag/v1.6.0
[1.6.1]: https://github.com/Cuxx/smart-grind-by-weight-singledose/releases/tag/v1.6.1
[1.6.2]: https://github.com/Cuxx/smart-grind-by-weight-singledose/releases/tag/v1.6.2
[1.6.3]: https://github.com/Cuxx/smart-grind-by-weight-singledose/releases/tag/v1.6.3
[1.6.4]: https://github.com/Cuxx/smart-grind-by-weight-singledose/releases/tag/v1.6.4
[1.6.5]: https://github.com/Cuxx/smart-grind-by-weight-singledose/releases/tag/v1.6.5
[1.6.6-beta.1]: https://github.com/Cuxx/smart-grind-by-weight-singledose/releases/tag/v1.6.6-beta.1
[1.6.6-beta.2]: https://github.com/Cuxx/smart-grind-by-weight-singledose/releases/tag/v1.6.6-beta.2
