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
- Target-free on-device Manual grinding with a 30-second safety cutoff
- Web flasher release artifacts for both board revisions
- Local light/dark web app with live status, grinder settings, history and
  screensaver management, plus selected-profile start/stop controls

## Priorities

1. **Reliable installation and recovery**
   - Publish tested V1 and V2 firmware from one release.
   - Require an explicit board-revision choice in the web flasher.
   - Retain USB recovery even after Wi-Fi updating is introduced.
2. **Wi-Fi and web OTA**
   - Non-blocking station connection with a first-run setup access point.
   - `smartgrind.local` mDNS access.
   - Stream a full firmware image directly to the inactive OTA partition.
   - Permit updates directly from the web app after an explicit confirmation,
     while retaining motor-stop, transfer-conflict and image-integrity guards.
   - Automatically pause idle Bluetooth for a web update instead of making the
     user wait for its startup timer or manually toggle either radio.
3. **Shared local-push API and live web UI**
   - Define a small, versioned WebSocket protocol for status, live weight,
     target, flow, grind phase and completed-session data.
   - Use the same protocol for the browser UI, simulator and integrations.
   - Keep all safety decisions in the firmware control loop; network clients
     request selected-profile actions but never drive the relay directly.
   - Keep history bounded on-device and provide browser CSV/JSON/raw downloads;
     retain BLE/Python export as the longer-term archive until web parity has
     been proven across releases.
4. **Native Home Assistant integration**
   - Follow the current GaggiMate model: zeroconf discovery plus a local-push
     WebSocket API and standard Home Assistant entities.
   - Package the integration for HACS separately from the firmware.
   - Treat MQTT as optional compatibility support, not the primary Home
     Assistant architecture.
5. **Additional reviewed features**
   - Design and validate an explicit motor-only first-run path for installations
     with no load cell, without weakening calibration checks for weight mode.
   - Revisit update security after the update workflow is stable: assess web
     authentication, BLE access control and signed firmware without making
     routine domestic updates cumbersome.
   - Evaluate bean tracking, basket/profile detection, screensavers and other
     outstanding community work as separate changes with attribution, tests
     and V1/V2 compatibility checks.

## Delivery status

### Next reliability release

The next release is being validated and is not published. Focused reliability
PRs cover motor timing, stale/disconnected scales, history, touchscreen events,
update recovery and operation ownership. The final settings-confirmation PR is
under review. Both firmware targets and host regressions have passed; physical
acceptance of this combined candidate is still outstanding. See
[release readiness](RELIABILITY_RELEASE.md).

Pulse-free finishing remains a separate tester-dependent draft. Learned cup
matching and variable-speed hardware work are not part of this release.

### Established baseline

The feature set is built for both maintained controller targets and both pass
CI. Earlier physical acceptance used the available V2 grinder; equivalent
community V1 hardware validation is welcome. These historical results do not
replace acceptance of the new reliability candidate.

| Area | Software status | Remaining acceptance work |
| --- | --- | --- |
| V1/V2 install and USB recovery | Complete | Recheck release packages before the next public release |
| Reproducible, cached firmware and simulator builds | Complete locally | Confirm cache restore and both hardware jobs on the first published CI run |
| Wi-Fi setup, discovery and OTA | Complete | Recheck the release package and router reconnect during routine release acceptance |
| Versioned live API and browser UI | Complete | Continue long-duration memory monitoring in routine use |
| Browser settings, analytics and screensavers | Complete | Continue cross-browser checks in routine use |
| Improv serial provisioning | Implemented | Validate browser-to-USB provisioning and failure recovery on V2 hardware |
| Native Home Assistant integration | Separate package passes tests, Ruff and hassfest; HACS CI configured | Validate HACS remotely plus discovery, entities, commands and reconnect behaviour on a live Home Assistant instance |

The complete physical release gate is maintained in
[V2 hardware acceptance](HARDWARE_ACCEPTANCE.md). None of the rows marked as
hardware validation pending should be promoted to complete until that checklist
has been run on the installed grinder.

The reviewed work will be split for publication according to the
[publication plan](PUBLICATION_PLAN.md), even though the consolidated branch is
used for integration and hardware testing.

## Reviewed upstream work

Original commit authorship is retained for incorporated contributions. Follow-up
hardening is kept in separate commits so the provenance remains clear.

| Upstream PR | Decision | Review outcome |
| --- | --- | --- |
| #111 BLE crash fixes | Integrated | Retained the contributor's commits and validated both hardware builds |
| #106 coast compensation | Integrated with follow-up | Restored the existing neutral `1.0` default instead of changing behaviour to `0.9` |
| #136 sensor-free time mode and pause | Integrated; supersedes #135 | Added state-ordering, completion and null-safety fixes |
| #114 screensaver | Integrated with follow-up | Added V2 compatibility, transactional image replacement and explicit upload acknowledgement |
| Issue #98 configurable auto-start threshold | Integrated independently | Added bounded touchscreen and web settings while retaining the existing 50 g default |
| Issue #96 automatic profile selection | Future design | Requires a deliberate learn-and-match workflow for multiple cup or portafilter weights; not safe to infer from one threshold |
| #82 alternate board/display support | Redesign required | The proposed pin/display assumptions conflict with the maintained V1/V2 targets and cannot be safely cherry-picked |

## Contribution and upstream policy

- Keep feature commits and pull requests narrowly scoped where practical.
- Build both hardware targets and run simulator tests before merging.
- Preserve original commit authorship when incorporating compatible GPLv3
  contributions from other forks or outstanding pull requests.
- Continue proposing useful changes to `jaapp/smart-grind-by-weight`.
- If upstream maintenance resumes, rebase or reshape these changes for review
  rather than requiring upstream to adopt this fork wholesale.
