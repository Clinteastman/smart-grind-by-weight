# Reliable settings application

This branch is work in progress and is not a released fix.

## Storage failures

Web profile updates check all eight stored values (selected profile, mode,
three weights and three times). A failed write returns failure and reloads
the stored profile state into memory. These separate writes are not an atomic
transaction: some values can be saved before another write fails. Reloading
keeps the running values consistent with the next reboot; it does not undo
successful writes.

The host regression test compiles the complete production profile controller.
It injects a failure at each write, compares the live state with a new
controller loaded from the resulting store, checks a subsequent successful
retry, and verifies that invalid requests do not write anything. Concurrent
snapshot tests also remain enabled.

Checkpoint validation: all 16 host tests and three standalone simulator policy
tests passed. Native WSL V1 and V2 firmware builds passed (local build 1).
No device flashing or physical acceptance test has been performed.

## Remaining before this fix can ship

- Check the other settings persistence paths, not just profiles.
- Refresh runtime settings and the cached web values after partial failures.
- Give each accepted settings request a result that can be checked after the
  queued work and touchscreen runtime refresh finish.
- Replace the web page's fixed 250 ms delay and unconditional success message
  with that actual result; report failures without claiming a rollback.
- Prevent settings application from overlapping a motor operation or update.
- Test the request/result lifecycle and the rendered web settings workflow.
- Complete V1/V2 builds, PR review and appropriate device acceptance before
  merge/release.
