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

## Contribution and upstream policy

- Keep feature commits and pull requests narrowly scoped where practical.
- Build both hardware targets and run simulator tests before merging.
- Preserve original commit authorship when incorporating compatible GPLv3
  contributions from other forks or outstanding pull requests.
- Continue proposing useful changes to `jaapp/smart-grind-by-weight`.
- If upstream maintenance resumes, rebase or reshape these changes for review
  rather than requiring upstream to adopt this fork wholesale.
