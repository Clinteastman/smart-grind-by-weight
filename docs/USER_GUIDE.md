# Everyday Use

This guide covers the touchscreen, local web interface and normal grinder
workflow after installation and calibration. New installations should begin at
the [documentation home](DOC.md).

**On this page:** [Manual grinding](#manual-grinding) ·
[Profiles](#grinding-profiles) · [Navigation](#navigation) ·
[Grind settings](#grind-settings) · [Automation](#automated-grind-flow) ·
[Bluetooth](#bluetooth-connectivity) · [Screensaver](#screensaver)

## Usage guide

### Manual Grinding

**Manual** is the first page in the main-screen carousel. Tap **START** to run
the grinder without a weight or time target, then tap **STOP** when the desired
amount has been ground. When a healthy load cell is fitted, the page shows a
large live weight and a large **TARE** button. Put the empty cup in place and
tap **TARE** if you want to weigh the result; taring is never automatic in
Manual mode. During grinding, the screen shows live weight and elapsed motor
time.

Manual start/stop works without a load cell; the scale readout and TARE button
simply report that the scale is unavailable. Manual mode deliberately skips
automatic taring, purging, predictive stopping, finishing pulses and
grind-history recording. It still adds the real motor run time to lifetime
statistics and stops automatically after 30 seconds. Use Single, Double or
Custom when you want a repeatable target and a recorded result.

### Grinding Profiles
All profiles are fully customizable. Default grind-by-weight targets (fallback time values shown in parenthesis):
- **Single**: 9 g (5 s)
- **Double**: 18 g (10 s)
- **Custom**: 21.5 g (12 s)

> 💡 **Tip** – the target label always shows the active unit (`g` or `s`). Long-press to edit in whichever mode you are currently using.

### Navigation
- **Swipe left/right** to navigate between menu tabs
- **Swipe up/down** on the ready screen to toggle between grind-by-weight and grind-by-time modes (when enabled in Menu → Grind Settings)
- **Tap** to select profiles or buttons
- **Long press** on profile targets to edit/customize them

> **Color cues:** The GRIND button background turns **red** in weight mode and **blue** in time mode, so you always know which behaviour is armed.

### Grind Settings
Access **Menu → Grind Settings** to configure:
- **Swipe Gestures**: Enable/disable vertical swipe gestures for mode switching (default: disabled)
- **Time Mode**: Directly toggle between Weight and Time modes regardless of swipe setting
- **Start on Cup**: Start the active profile automatically when the scale gains the configured cup threshold (50 g by default) after a short post-boot warmup
- **Return on Removal**: Leave the completion screen as soon as that cup weight drops back off the scale
- **Motor Latency** *(Advanced)*: View or manually adjust the minimum reliable
  motor pulse from 30–300 ms in 5 ms steps. Use **Menu → Tune Pulses** first;
  this manual control is a fallback when automatic tuning cannot complete.
- **Purging** *(Advanced)*: Control how the grinder saturates itself before weight-mode grinding
  - **Prime mode**: Keeps the coffee used to saturate the grinder, continues immediately
  - **Purge mode** (default): Prompts you to discard stale grinds before continuing
  - **Amount slider**: Configure purge/prime amount (0.1g-5.0g, default 1.0g). Amount is a minimum target; actual output will be slightly higher.
  - **"Keep purge grinds from now on" checkbox**: Appears during purge confirmation - switches to Prime mode when checked

  *Explanation:* The time between motor start and grinds hitting the cup (grind latency) is used to predict the coast time (how long grinds will keep coming after the motor is disengaged). Purging clears stale coffee and saturates the grinder with fresh grounds, ensuring accurate latency detection. If you prefer to keep all coffee without manual intervention, select Prime mode.

### Basic Operation
These steps describe the default grind-by-weight workflow:
1. Select profile by tapping on the main screen
2. Long press the profile target to edit/customize the weight if needed
3. Place the dosing cup on the scale platform
4. Press the GRIND button – the scale will tare automatically
5. The system grinds to the precise target weight using the predictive algorithm
6. GRIND COMPLETE shows the final settled weight in grams (with statistics)

> Optional automation (Menu → Grind Settings): enable auto-start and set the cup threshold below the empty cup or portafilter weight. The system waits for the load cell to gather enough quiet samples before arming itself, then auto-return jumps back to Ready whenever that cup is lifted off again.

Need the stock timed run? Enable swipe gestures in **Menu → Grind Settings**, then swipe up or down on the ready screen before you start; the GRIND button background turns blue to confirm time mode is active (red = weight). Alternatively, use the direct **Time Mode** toggle in the menu.

> **Time mode pulse button:** In time mode completion, a "+" button appears next to OK for 100ms additional grinding pulses.

From the local web dashboard, choose Single, Double or Custom and use the round
play/stop button to request the same target grind remotely. The request passes
through the firmware's normal checks and state machine; Manual mode remains an
on-device control.

### Quick Scale View
Need a simple live readout? Open **Menu → Scale** to jump into a full-screen weight display. Entering the page automatically tares the scale (using the same blocking overlay as the main workflow), and a large `TARE` button at the bottom lets you re-zero manually whenever you need.

### Display Modes
- **Arc Layout**: Clean, minimal arc-based interface
- **Nerdy Layout**: Detailed charts showing flow rates and real-time grinding analytics
- **Switching**: Tap anywhere on grind screen to switch between layouts during grinding
- **Screensaver**: A custom or built-in design can show on startup or when the
  display dims. An optional later panel-off stage protects the AMOLED during
  long idle periods.

---

## User interface navigation

```
Main Screen (swipe left/right between tabs, up/down to toggle weight/time mode if enabled)
|
+-- Manual
|   |-- Live elapsed motor time
|   \-- START / STOP button (30s safety limit)
|
+-- Single Profile
|   |-- Weight display (long press to edit)
|   \-- GRIND button (red=weight, blue=time)
|
+-- Double Profile
|   |-- Weight display (long press to edit)
|   \-- GRIND button (red=weight, blue=time)
|
+-- Custom Profile
|   |-- Weight display (long press to edit)
|   \-- GRIND button (red=weight, blue=time)
|   \-- Time mode completion: OK + PULSE buttons
|
\-- Menu (scrollable hub)
    |
    +-- Tools (quick actions)
    |   |-- Scale (live weight view with Tare action)
    |   |-- Calibrate (launch calibration workflow)
    |   |-- Tune Pulses (auto-tune motor latency)
    |   \-- Motor Test (1s safety pulse)
    |
    +-- Settings
    |   +-- Bluetooth
    |   |   |-- Bluetooth toggle (30m timer)
    |   |   |-- Bluetooth startup toggle (configurable auto-enable)
    |   |   |-- Connection status display
    |   |   \-- Auto-disable timer display
    |   |
	    |   +-- Display
	    |   |   |-- Normal brightness slider
	    |   |   |-- Screensaver brightness slider
	    |   |   |-- Screensaver startup/idle toggles
	    |   |   \-- Optional Turn Display Off toggle
    |   |
    |   \-- Grind Settings
    |       |-- Swipe Gestures toggle (enable/disable vertical swipes)
    |       |-- Time Mode toggle (direct weight/time mode selection)
    |       |-- Start on Cup toggle and configurable cup threshold
    |       |-- Return on Removal toggle (drop back to Ready when that weight leaves)
    |       |-- Purging (Prime/Purge radio buttons)
    |       |-- Amount slider (0.1g-5.0g for purge/prime operation)
    |       |-- Motor latency slider (30-300ms manual Pulse Tune fallback)
    |       \-- Coast compensation slider
    |
    \-- Info
        +-- Diagnostics
        |   |-- Load Cell Status (calibration flag, calibration factor)
        |   |-- Noise Floor (std dev g/ADC, noise level indicator)
        |   |-- Active diagnostic warnings
        |   \-- Reset diagnostics button
        |
        +-- System Info
        |   |-- Firmware version & build number
        |   |-- Real-time weight sensor data (instant, samples, raw)
        |   |-- Uptime display
        |   \-- Memory usage
        |
        +-- Logs & Data
        |   |-- Logging toggle (enable/disable session file writing)
        |   |-- Sessions / Events / Measurements counters
        |   |-- Purge Logs button
        |   \-- Factory Reset button
        |
        \-- Lifetime Stats
            |-- Refresh Stats button
            |-- Total grinds, shots, weight
            |-- Motor runtime, uptime, accuracy
            \-- Pulse counts

During Grinding:
|-- Weight/elapsed display & progress
|-- Tap anywhere: Arc ↔ Nerdy display modes
|-- STOP button
\-- Purge Confirmation (appears in Purge mode after grinder saturation)
    |-- "Grinder Purged" title
    |-- Instruction message
    |-- "Keep purge grinds from now on" checkbox
    \-- CONTINUE button
```

---

## Automated grind flow

Want the scale to run itself? Enable the automation toggles in **Menu → Grind Settings**:

- **Start on Cup**: As soon as a cup or portafilter adds at least the configured threshold, the active profile tares and begins grinding automatically. The default is 50 g; set it safely below the empty accessory weight but above incidental touches or vibration.
- **Return on Removal**: When the cup weight drops away after completion, the grinder exits the results screen and returns to Ready. Useful for keeping the workflow hands-free between shots.

Both automation settings rely on the same smoothed weight deltas used for flow detection, so no extra calibration is required. Leave them disabled if you prefer manual control or experience false triggers with lighter accessories.

---

## Bluetooth connectivity

Bluetooth can be configured in **Menu → Bluetooth** with optional auto-startup (5-minute timer) or manual control (30-minute timer when manually enabled). The blue Bluetooth symbol in the top-right corner indicates when active. Bluetooth enables wireless firmware updates via BLE OTA, legacy grind-data export and device management. Grind session logging is configurable in **Menu → Data → Logging** and is enabled by default so the local web History page works immediately; disable it if you do not want sessions written to flash.

---

## Screensaver

Open `http://smartgrind.local` and choose **Settings → Display & screensaver**.
Select the built-in Minimal, Orbit or Black AMOLED design, the live GaggiMate
status view, or upload a normal photo; the browser crops and converts custom
images to the display's 280 × 456 RGB565 format before sending them to the
grinder.

- **Custom image**: Upload a normal photo from the web interface; conversion
  happens in the browser before it is sent to the grinder.
- **GaggiMate status**: Enter the machine hostname (normally
  `gaggimate.local`) or its IP address. While both devices are on the same
  Wi-Fi network, the screensaver shows temperature and readiness, then live
  shot time, phase, pressure and flow during an extraction. It uses
  GaggiMate's existing WebSocket API and falls back to its compact HTTP status
  endpoint, so no modified GaggiMate firmware is required.
- **Timing settings**: Configure when the screensaver starts and its startup
  duration in the local web app. You can optionally turn the panel fully off
  after a second delay; this is disabled by default, so the current
  always-visible screensaver behaviour is preserved after updating.
- **Device settings**: Brightness, startup/idle screensaver and **Turn Display
  Off** toggles remain available under **Menu → Display** and are synchronized
  with the web settings. The web app controls the additional off delay.
- **Wake behaviour**: Touching the dark panel, changing the scale load or
  starting a grind wakes it. The first wake touch is consumed so it cannot
  accidentally press the control underneath.
- **Startup behavior**: On normal Ready boots, the image is drawn early while the full UI initializes, then the regular timed screensaver overlay takes over.
- **OTA behavior**: During BLE OTA updates and OTA failure warnings, the screensaver is disabled so progress and recovery prompts stay visible.

---

**Previous:** [Firmware and first setup](FIRMWARE_SETUP.md) ·
**Next:** [Diagnostics and data →](DIAGNOSTICS_AND_DATA.md)
