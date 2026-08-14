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
   - HACS and hassfest validation, then a pre-release before any stable release

Each pull request must state which commits came from an existing contributor and
which are follow-up review fixes. Do not squash away original authorship.

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

