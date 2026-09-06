# Invalid scale readings

Weight-mode grinding requires a usable sample within 500 ms. A responsive ADC
is not sufficient: disconnected DOUT held low and readings at either ADC rail
must not refresh that timer.

The HX711 driver checks DOUT releases after every conversion, not just startup.
The sensor rejects readings within the existing 65,535-count rail margin before
feeding its filter or progressing tare. Valid samples recover normal sampling;
a grind already stopped for a scale error does not restart automatically.

Time and Manual modes retain their existing scale-independent operation.
Check wiring and mechanical preload if the diagnostic log reports ADC rails.

Host regression tests exercise the production conversion for all three gain
clock counts, disconnected DOUT, rail boundaries, freshness, and tare handling.
Physical disconnect testing with supervised motor operation remains required
before claiming hardware verification.
