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
3. **Wi-Fi, live web UI and guarded OTA**
   - Non-blocking station/setup-AP lifecycle and mDNS
   - Physically armed full-image web OTA with motor/heap/image guards
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

Verified on 2026-08-14 before physical acceptance and publication:

| Change | Verified commit | Local evidence |
| --- | --- | --- |
| Reviewed community grind fixes | `8e3d27e` | V1 and V2 firmware builds pass independently |
| Screensaver support | `e9c7ad9` | V1 and V2 firmware builds pass independently |
| Wi-Fi, live web UI and guarded OTA | `b392efb` | V1 and V2 builds pass independently; review aligned the public phase contract with clients |
| Release and build reliability | `907661c` | Consolidated V1/V2 builds and all three simulator tests pass; cached repeats are about 25 seconds per firmware target and 9 seconds for the simulator |
| Native Home Assistant integration | `f823d17` | Thirteen API tests, Ruff and hassfest pass; remote HACS validation awaits repository publication |

These results prove that the focused branches compile without relying on later
changes in the stack. They do not replace the physical and remote-CI gates
below.

The full local diff review is complete across all four firmware changes. Its
last functional finding was an API mismatch where internal controller phases
were emitted instead of the documented stable phase values; `b392efb` fixes the
firmware mapping and `f823d17` enforces the same contract in Home Assistant.

Release-asset validation on the consolidated source produced:

| Target | Full image | Web/BLE OTA patch | Verification |
| --- | --- | --- | --- |
| V1 | 2,393,904 bytes; SHA-256 `3e585a8450cad9b284c5069a8dce2bced8cefd5f36396d6f51d7a8bc1b3997ba` | 1,759,922 bytes | detools 0.47 restores the full image byte-for-byte |
| V2 | 2,367,520 bytes; SHA-256 `0c661d74c446b89586faac5cffa1026bd38b87cb45cca148fa673421059dfce4` | 1,742,021 bytes | detools 0.47 restores the full image byte-for-byte |

Both full images begin with the ESP32 image magic byte `0xE9` and fit the
3,072 KiB application partitions. Both patches fit the 2,048 KiB patch
partition.

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
