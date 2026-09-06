# Operation reservation (development branch)

This change is still being integrated; it is not a released safety guarantee.

Normal grinding, Pulse Tune, Motor Test, Bluetooth firmware transfer and Wi-Fi
update preparation now reserve the same operation slot before modifying hardware.
A competing start is rejected.
Reservations stay held until the motor is stopped and cleanup has finished.
Bluetooth updates keep the slot through a successful reboot, or a watchdog
recovery failure that requires restarting.

Motor Test allocates its completion timer before firing a pulse. If allocation
fails, the motor does not start. Repeated starts and unrelated timer callbacks
cannot replace or finish the running test.

Normal grinding keeps ownership during pause/resume, purge confirmation and
completion so extra timed pulses cannot overlap another operation. Dismissing the
result releases it after history cleanup and a motor stop. An idle controller's
stop request does not touch a motor owned by a diagnostic operation.

Wi-Fi holds ownership throughout its preparation window, upload/download and
pending reboot. An expired window cannot accept a new upload. Recoverable failures
abort the firmware writer before releasing ownership; failures requiring a reboot
keep it. A generation token on each upload prevents a late disconnect or data
callback from affecting a later upload. Preparation and upload transitions are
serialized so expiry cannot release the slot after an upload starts.

Before shipping: independent review, combined release validation and hardware
checks are still required. This branch is stacked on the other reliability fixes;
it must not bypass their individual review and merge gates.

The reservation uses a generation token. Cleanup must release the token it
acquired, not whichever operation is currently running. A late cleanup from a
previous generation is ignored. Generation values repeat after 2^31 successful
reservations; they are not persistent identifiers across reboot.

The host tests exercise concurrent acquisition, stale release, real Bluetooth
failure recovery, real tuning cancellation, and real Motor Test methods with
timer-allocation failure. They do not prove physical motor timing or OTA success.

The Wi-Fi test executes production preparation, update tick, readiness, upload,
GitHub task admission and recovery methods. It covers simultaneous uploads,
expiration, failed writes/validation/task creation, stale callbacks and reboot
lockout. It also executes the real grind admission checks against that same
reservation. Session tests cover release on dismissal, history queue failure and
retention during an additional pulse.

Local checkpoint validation: all 15 host tests and all three standalone policy
tests passed. Native WSL2 firmware builds passed for V1 (89.108 seconds) and V2
(93.169 seconds). One earlier profile-snapshot stress test hit its 30-second
deadline while firmware compilation was running; the complete suite then passed
in 13.624 seconds with compilation stopped. Keep that timing sensitivity visible
during review; do not count the timed-out run as a pass.

Full integration checkpoint: all 16 host tests and the three standalone policy
tests passed. Final native WSL2 builds passed for V1 (27.911 seconds) and V2
(25.924 seconds), local build #1. No hardware was flashed or operated.
