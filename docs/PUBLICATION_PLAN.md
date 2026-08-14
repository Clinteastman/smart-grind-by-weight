# Publication plan

The consolidated development branch is used to prove that the complete feature
set works together. Publication should still use focused changes so reviewers
can understand provenance, risk and rollback boundaries.

## Proposed pull-request sequence

1. **Reviewed community grind fixes**
   - BLE crash fixes from upstream PR #111
   - Coast compensation from #106, retaining the neutral `1.0` default
   - Sensor-free time mode and pause/resume from #136, which supersedes #135
   - Our state-ordering, completion and null-safety follow-up
2. **Screensaver support**
   - Original #114 contributor commits with authorship intact
   - V2 display compatibility, transactional image replacement and explicit
     uploader acknowledgement
3. **Wi-Fi, live web UI and OTA**
   - Non-blocking station/setup-AP lifecycle and mDNS
   - Direct full-image web OTA with confirmation plus motor/heap/image guards
   - Bounded v1 WebSocket API and responsive live dashboard
   - Official Improv Serial v1 USB provisioning
   - Stable device identity for the separate Home Assistant integration
4. **Release and build reliability**
   - Exact dependency/tool versions, bounded compiler parallelism and cache
   - V1/V2 CI, simulator CI, dual-board release assets and flasher guidance
5. **Native Home Assistant integration**
   - Publish from its own single-integration repository after live validation
   - Local tests, Ruff and hassfest pass; confirm HACS CI after publication,
     then create a pre-release before any stable release

Each pull request must state which commits came from an existing contributor and
which are follow-up review fixes. Do not squash away original authorship.

## Local verification snapshot

Verified on 2026-08-14 before full installed-grinder acceptance and publication:

| Change | Verified commit | Local evidence |
| --- | --- | --- |
| Reviewed community grind fixes | `8e3d27e` | V1 and V2 firmware builds pass independently |
| Screensaver support | `e9c7ad9` | V1 and V2 firmware builds pass independently |
| Wi-Fi, live web UI and OTA | `b392efb` | V1 and V2 builds pass independently; review aligned the public phase contract with clients |
| Release and build reliability | `907661c` | Consolidated V1/V2 builds and all three simulator tests pass; cached repeats are about 25 seconds per firmware target and 9 seconds for the simulator |
| Native Home Assistant integration | `f823d17` | Thirteen API tests, Ruff and hassfest pass; remote HACS validation awaits repository publication |

These results prove that the focused branches compile without relying on later
changes in the stack. They do not replace the physical and remote-CI gates
below.

The branch-wide consolidated review was repeated after the settings, history
and screensaver web UI changes. It covered the complete source/workflow diff,
page scripts, Python tooling and public routes. The review fixed exact API route
matching, safe active-slot USB recovery, Windows full-BLE uploads, Python 3.8
typing compatibility and the signed-error consistency calculation. No remote
motor-start route is exposed. Visual and responsive browser acceptance was then
completed against the live V2 grinder, and the accepted dashboard, history and
settings views are captured in the README.

Runtime display-settings acceptance was also exercised on the V2 board: the
existing 30-second timeout, 35% dim brightness and orbit screensaver were
re-applied through `POST /api/v1/settings`. The controller acknowledged the
runtime refresh immediately and entered the enabled screensaver after 30
seconds without a reboot.

Boot-time scale acceptance was repeated after adding the automatic tare. From
the first WebSocket frame after a Wi-Fi OTA reboot, twelve consecutive idle
states reported `0.00 g` and the selected profile target `21.5 g`; later
sampling heartbeats confirmed a completed tare with stable readings of
`0.014 g` and `0.002 g`, rather than the assembled grinder's raw preload.

Final consolidated application builds after the web UI review produced:

| Target | Full image | Build verification |
| --- | --- | --- |
| V1 | 2,491,584 bytes; SHA-256 `3a341d1d60c93102cd8f7723a4cb223ba0b2aefa6094cebcc54beabd2b5f3fb6` | Stable 1.5.0 Build 29 passes; 79.2% application flash and 23.3% RAM |
| V2 | 2,465,392 bytes; SHA-256 `37906adc42fb7b7f6efb449b8f496fec5643b028304c02599ae34fbbb37c4e8e` | Stable 1.5.0 Build 29 passes; 78.4% application flash and 23.3% RAM; the preceding release-candidate artifact was installed by direct web OTA and verified through the Wi-Fi diagnostic log |

Both full images begin with the ESP32 image magic byte `0xE9` and fit the
3,072 KiB application partitions. Generate and byte-verify release delta
patches from these exact reviewed images only after the final source is frozen.

The final local gate also exposed and fixed an unsafe shared PlatformIO object
cache: V1 and V2 can compile LVGL with different flags, so their cached objects
must never share a directory. Local tooling and both GitHub workflows now use
hardware-target-specific cache paths. A clean V2 build passed after the old
mixed cache was quarantined.

The same V2 canary retained its Wi-Fi credentials and settings, served the
updated page at `smartgrind.local`, returned exact 400/404 errors for malformed
and missing history sessions, reported healthy UI/FileIO/control tasks, and
remained `IDLE`. Motor actuation was deliberately not tested while the grinder
was disassembled.

Direct web OTA was then validated without a physical arm action or token. The
preparation request temporarily disabled idle Bluetooth, increased free
internal heap from about 51 KiB to 118 KiB, accepted and validated the complete
2,465,392-byte V2 image, returned HTTP 200, and restarted with reset reason
`SW (esp_restart)`. Wi-Fi and all three profiles survived. An expired prepared
window, a deliberately invalid image, and an interrupted upload all cleanly
restarted into the existing valid firmware, restored Wi-Fi and Bluetooth, and
returned internal heap to about 51 KiB. The invalid image returned HTTP 400
before recovery. The earlier mid-upload resets were traced to suspended
weight/control tasks remaining subscribed to the task watchdog; OTA suspension
now removes and restores those subscriptions.

The final V2 canary also returned its 2,717-byte retained startup log over
`GET /api/v1/logs` after that Wi-Fi OTA. It included reset reason, filesystem,
load-cell, grinder, UI, Bluetooth and task initialization plus mDNS/HTTP startup,
demonstrating routine post-assembly diagnosis without USB serial.

The installed dashboard selected Single (`9.0 g`), Double (`18.0 g`) and Custom
(`21.5 g`) through the queued profile API while the motor remained off, kept the
web highlight and target synchronized, and restored Custom afterwards. Six
consecutive idle browser samples displayed `0.00 g/s` with the display-only
flow filter enabled; controller inputs and stored session traces are unchanged.

Final installed-grinder acceptance then completed a real 18 g grind. Motor
control and the physical interface behaved as before, the browser followed the
grind live with a stable display-only flow value, and completion replaced the
live trace with the full-resolution recorded weight and flow history. The
resulting history graph matched the retained session data and required no page
reload or manual dismissal before review.

## Readiness gate

- The consolidated V1 and V2 builds and simulator suite must pass first.
- Run [V2 hardware acceptance](HARDWARE_ACCEPTANCE.md) on the exact consolidated
  commit intended for publication.
- Repeat the relevant V1/V2 build and simulator checks after splitting branches
  to catch missing cross-PR dependencies.
- Let GitHub CI complete and review every changed file plus generated release
  assets before changing any pull request from draft to ready.
- Publish the Home Assistant integration only after its mock-device tests,
  hassfest/HACS checks and live discovery/reconnect tests pass.
