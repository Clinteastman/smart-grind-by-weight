# V2 hardware acceptance checklist

Run this checklist on the installed grinder before publishing the Wi-Fi/API
release. Keep USB recovery available throughout. The known working V2 motor
connection is the grey trigger wire on GPIO16; GPIO18 is the touchscreen
interrupt on this controller and must not be used for the relay.

## 1. Baseline and safety

- Photograph or record the board marking. The newer SH8601 controller can still
  be labelled `Rev1.1`; do not select firmware from that text alone.
- Confirm the V2/SH8601 firmware is selected and retain the last known-good V2
  USB image.
- Power on with the grinder motor disconnected first. Confirm the AMOLED UI,
  touch input, load-cell weight and all screen swipes work normally.
- Reconnect the trigger only after the UI is stable. Confirm GPIO16 starts and
  stops the relay and that GPIO18 remains reserved for touch.

## 2. Grind behaviour

- Run weight mode and confirm progress, coast compensation, final settling and
  completion are correct.
- Run time mode with the load cell connected, then with it unavailable. Confirm
  completion and pause/resume do not dereference the missing sensor.
- Confirm short swipes starting near the centre consistently change screen and
  retain visible transition frames.
- Leave the controller idle long enough for the standard and custom
  screensavers; confirm touch dismissal and image fallback.

## 3. Wi-Fi provisioning

- Clear test credentials and confirm the secured `SmartGrind-*` setup access
  point appears with the on-device password/QR details.
- Save a 2.4 GHz network through the captive page. Confirm the controller
  restarts, reconnects and remains responsive during connection attempts.
- Clear credentials again and provision through Improv Serial v1 over USB.
  Confirm the client receives the grinder URL and that a deliberately wrong
  password returns a bounded failure rather than hanging.
- Power-cycle the controller and router independently. Confirm automatic
  reconnect, `.local` access and `_smartgrind._tcp` discovery return.

## 4. Live web UI and load

- Open the local device page and run a grind. Confirm weight, flow, phase,
  progress and the graph update live while the physical UI stays responsive.
- Confirm **Stop grind** stops an active grind, **Dismiss result** clears only a
  completed/timed-out result, and neither interface offers remote motor start.
- Leave the page open for 30 minutes and repeatedly disconnect/reconnect Wi-Fi.
  Record free internal heap before and after; it must recover rather than trend
  downwards.
- Open more than four clients and confirm excess/slow clients are rejected
  without affecting the grinder control loop.

## 5. Guarded OTA and recovery

- Confirm an unarmed upload is rejected and an armed window is refused while
  grinding.
- Physically arm OTA from the grinder UI, upload the matching full V2 firmware
  image, and confirm the reported version/build after restart.
- Interrupt one upload before completion and confirm the existing application
  still boots. Then complete a normal upload.
- Confirm USB flashing can still recover the controller after the OTA tests.

## 6. Home Assistant

- Install the pre-release custom integration in a test Home Assistant instance;
  do not replace an existing working setup without a backup.
- Confirm automatic discovery creates one device using its stable identifier,
  and that an IP-address change updates the existing entry instead of creating a
  duplicate.
- Confirm weight/flow update at up to 5 Hz during grinding, phase and safety
  changes are immediate, and idle updates back off to once every five seconds.
- Confirm stop/dismiss acknowledgements, offline availability, exponential
  reconnect, and recovery after both grinder and Home Assistant restarts.

Record firmware commit, Home Assistant integration commit, test date and any
deviations in the release notes before marking the pull requests ready.
