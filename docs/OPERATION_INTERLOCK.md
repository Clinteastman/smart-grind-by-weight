# Operation reservation (development branch)

This change is still being integrated; it is not a released safety guarantee.

Pulse Tune, Motor Test and Bluetooth firmware transfer now reserve the same
operation slot before modifying hardware. A competing start is rejected.
Reservations stay held until the motor is stopped and cleanup has finished.
Bluetooth updates keep the slot through a successful reboot, or a watchdog
recovery failure that requires restarting.

Motor Test allocates its completion timer before firing a pulse. If allocation
fails, the motor does not start. Repeated starts and unrelated timer callbacks
cannot replace or finish the running test.

Remaining integration before this can ship:

- Normal grinding, including completion and additional timed pulses.
- Wi-Fi update preparation, uploads, recovery and pending reboot.
- Tests covering all combined entry points and hardware checks.

The reservation uses a generation token. Cleanup must release the token it
acquired, not whichever operation is currently running. A late cleanup from a
previous generation is ignored. Generation values repeat after 2^31 successful
reservations; they are not persistent identifiers across reboot.

The host tests exercise concurrent acquisition, stale release, real Bluetooth
failure recovery, real tuning cancellation, and real Motor Test methods with
timer-allocation failure. They do not prove physical motor timing or OTA success.

Local checkpoint validation: all 15 host tests and all three standalone policy
tests passed. Native WSL2 firmware builds passed for V1 (89.108 seconds) and V2
(93.169 seconds). One earlier profile-snapshot stress test hit its 30-second
deadline while firmware compilation was running; the complete suite then passed
in 13.624 seconds with compilation stopped. Keep that timing sensitivity visible
during review; do not count the timed-out run as a pass.
