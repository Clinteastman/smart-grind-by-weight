# Community roadmap

This fork keeps a tested, installable line available while changes continue to
be proposed to the original project as focused pull requests. The intention is
to make future upstream reintegration straightforward, not to replace or erase
the original project or its authorship.

## Current baseline

- Waveshare 1.64-inch V1 and V2 firmware targets
- Correct V2 display driver and external GPIO documentation
- Desktop simulator with deterministic grind and UI performance tests
- Faster display updates and more reliable short swipe gestures
- Web flasher release artifacts for both board revisions

## Priorities

1. **Reliable installation and recovery**
   - Publish tested V1 and V2 firmware from one release.
   - Require an explicit board-revision choice in the web flasher.
   - Retain USB recovery even after Wi-Fi updating is introduced.
2. **Wi-Fi and safe web OTA**
   - Non-blocking station connection with a first-run setup access point.
   - `grindbyweight.local` mDNS access.
   - Stream a full firmware image directly to the inactive OTA partition.
   - Permit updates only while the motor is stopped and a physical update
     window has been opened on the device.
3. **Shared local-push API and live web UI**
   - Define a small, versioned WebSocket protocol for status, live weight,
     target, flow, grind phase and completed-session data.
   - Use the same protocol for the browser UI, simulator and integrations.
   - Keep all safety decisions in the firmware control loop; network clients
     request actions but never drive the relay directly.
4. **Native Home Assistant integration**
   - Follow the current GaggiMate model: zeroconf discovery plus a local-push
     WebSocket API and standard Home Assistant entities.
   - Package the integration for HACS separately from the firmware.
   - Treat MQTT as optional compatibility support, not the primary Home
     Assistant architecture.
5. **Additional reviewed features**
   - Evaluate bean tracking, basket/profile detection, screensavers and other
     outstanding community work as separate changes with attribution, tests
     and V1/V2 compatibility checks.

## Delivery status

| Area | Software status | Remaining acceptance work |
| --- | --- | --- |
| V1/V2 install and USB recovery | Complete | Recheck release packages before the next public release |
| Wi-Fi setup, discovery and guarded OTA | Implemented | Validate station/AP reconnect, on-device arming and a full V2 OTA cycle on hardware |
| Versioned live API and browser UI | Implemented | Validate sustained live graphing, touch responsiveness and memory headroom on V2 hardware |
| Improv serial provisioning | Implemented | Validate browser-to-USB provisioning and failure recovery on V2 hardware |
| Native Home Assistant integration | Implemented in a separate local package | Validate discovery, entities, commands and reconnect behaviour on a live Home Assistant instance |

## Reviewed upstream work

Original commit authorship is retained for incorporated contributions. Follow-up
hardening is kept in separate commits so the provenance remains clear.

| Upstream PR | Decision | Review outcome |
| --- | --- | --- |
| #111 BLE crash fixes | Integrated | Retained the contributor's commits and validated both hardware builds |
| #106 coast compensation | Integrated with follow-up | Restored the existing neutral `1.0` default instead of changing behaviour to `0.9` |
| #136 sensor-free time mode and pause | Integrated; supersedes #135 | Added state-ordering, completion and null-safety fixes |
| #114 screensaver | Integrated with follow-up | Added V2 compatibility, transactional image replacement and explicit upload acknowledgement |
| #82 alternate board/display support | Redesign required | The proposed pin/display assumptions conflict with the maintained V1/V2 targets and cannot be safely cherry-picked |

## Contribution and upstream policy

- Keep feature commits and pull requests narrowly scoped where practical.
- Build both hardware targets and run simulator tests before merging.
- Preserve original commit authorship when incorporating compatible GPLv3
  contributions from other forks or outstanding pull requests.
- Continue proposing useful changes to `jaapp/smart-grind-by-weight`.
- If upstream maintenance resumes, rebase or reshape these changes for review
  rather than requiring upstream to adopt this fork wholesale.
