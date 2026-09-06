# Recorded grind duration

Completed and timed-out grinds record their finish time when the controller
enters the terminal phase. Waiting for the background history save, dismissing
the result later, or retrying a full save queue must not extend that duration.

The final motor-on interval uses the same captured finish time. Timestamp zero
is valid, including across the millisecond clock rollover. Motor time remains
an estimate from sampled motor state, not a measurement of physical shaft motion.

This does not change grind targets, timeout limits, pause behaviour, the history
file format, or old saved records. Additional time-mode pulses remain outside
the already completed record. Cancelled sessions retain their existing behaviour.

Host regression tests execute production logger finalization with controlled
clocks and the controller's queue/dismiss/additional-pulse methods with a bounded
queue double. Firmware builds and physical timing acceptance are separate gates.
