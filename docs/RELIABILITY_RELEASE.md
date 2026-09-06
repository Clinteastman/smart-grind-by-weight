# Reliability release readiness

Release preparation for **1.5.8** follows the reviewed candidate and V2 acceptance
below. The GitHub releases page is authoritative for publication status.

## Software evidence

- Focused fixes for motor timing, Bluetooth recovery, scale faults, history,
  controller ownership and touchscreen events have been reviewed and merged.
- [PR51](https://github.com/Clinteastman/smart-grind-by-weight/pull/51) adds checked
  settings persistence and runtime confirmation. Findings about Pulse Tune
  write failures and post-save runtime reads have been fixed. Current-head review
  passed with no unresolved findings; the PR is merged.
- Candidate code `e9f437b81b11835f52af47391f4f5529032931cb` passes 26 host
  regression tests, native WSL2 V1/V2 builds (local build 1), and all four GitHub
  checks on that exact candidate.
- Browser-fixture checks cover success, pending timeout, busy refusal, partial
  save and failed reload. Desktop and 390-pixel mobile layouts were inspected.
  The fixture does not provide a real motor, storage or WebSocket telemetry.

## V2 acceptance — 6 September 2026

- Wi-Fi upload was accepted and the board restarted on candidate `e9f437b+`,
  build 1. Exposed grinder/display settings matched before and after installation.
- With the owner beside the grinder, an 18 g web start was acknowledged. The
  motor ran and stopped automatically; completion reported 18.03 g.
- History session 126 records COMPLETE, 13.491 seconds, 12 events and 675
  measurements. The completion record remained available after the stop request.
- Brightness changed from 100 to 90 and back. Both real save/result requests
  returned `saved`; all exposed settings were restored exactly. Uptime increased
  from 205649 to 206367 ms, confirming no reboot was needed.

This does not establish V1 physical acceptance. No deliberate sensor-disconnection,
interrupted OTA or interrupted-grind emergency-stop test was performed. Host
fault-injection regressions supplement, but do not replace, physical tests.

## Publication checklist

1. Finish the current-head review, inspect inline findings and resolve valid
   issues. Do not treat a completed review status alone as approval.
2. Record supervised installation and normal-dose acceptance as above. Never
   operate the motor unattended for release checks.
3. Record hardware results separately from host and browser tests. V2 results
   do not establish V1 physical acceptance; invite V1 feedback explicitly.
4. Merge the accepted candidate, finalize the versioned changelog and README,
   then build the release from that reviewed main commit.
5. Verify both firmware variants, OTA assets, manifests and release notes before
   publishing the draft. Verify the web flasher discovers the published release.

The optional pulse-free draft and tester-dependent hardware enhancements are
excluded. Reports about Wi-Fi restarts or custom images remain open until the
reporters can confirm their results with the published fix.
