# Reliability release readiness

The latest published release is **1.5.7**. The next reliability update is not
published, and the candidate has not been flashed or physically tested during
this review cycle.

## Software evidence

- Focused fixes for motor timing, Bluetooth recovery, scale faults, history,
  controller ownership and touchscreen events have been reviewed and merged.
- [PR51](https://github.com/Clinteastman/smart-grind-by-weight/pull/51) adds checked
  settings persistence and runtime confirmation. Findings about Pulse Tune
  write failures and post-save runtime reads have been fixed; final review
  is pending.
- Candidate code `e9f437b81b11835f52af47391f4f5529032931cb` passes 26 host
  regression tests and native WSL2 V1/V2 builds (local build 1). GitHub checks
  must also pass on this exact candidate before acceptance.
- Browser-fixture checks cover success, pending timeout, busy refusal, partial
  save and failed reload. Desktop and 390-pixel mobile layouts were inspected.
  The fixture does not provide a real motor, storage or WebSocket telemetry.

## Required before publication

1. Finish the current-head review, inspect inline findings and resolve valid
   issues. Do not treat a completed review status alone as approval.
2. Supervise candidate installation on the matching board. Confirm startup,
   retained calibration/settings, physical start/stop, a normal dose and its
   saved history. Verify web settings apply without rebooting and Wi-Fi update
   recovery works. Never operate the motor unattended for these checks.
3. Record hardware results separately from host and browser tests. V2 results
   do not establish V1 physical acceptance; invite V1 feedback explicitly.
4. Merge the accepted candidate, finalize the versioned changelog and README,
   then build the release from that reviewed main commit.
5. Verify both firmware variants, OTA assets, manifests and release notes before
   publishing the draft. Verify the web flasher discovers the published release.

The optional pulse-free draft and tester-dependent hardware enhancements are
excluded. Reports about Wi-Fi restarts or custom images remain open until the
reporters can confirm their results with the published fix.
