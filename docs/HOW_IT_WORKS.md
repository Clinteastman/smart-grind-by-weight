# How Smart Grind Works

This page explains the predictive grind-by-weight algorithm, motor-latency
model, safety boundaries and common design questions. It is background reading,
not required for normal installation or use.

**On this page:** [Grinding algorithm](#grinding-algorithm) ·
[Motor response](#motor-response-latency-model) ·
[Frequently asked questions](#frequently-asked-questions)

## Algorithm details

### Grinding Algorithm

The system uses a **zero-shot learning algorithm** requiring no prior knowledge or manually tuned variables. It instantly adapts to changes in temperature, humidity, grinding coarseness, bean type, and hardware characteristics.

**Multi-Phase Approach:**

1. **Initialization & Taring Phase**
   - Automatic tare on grind button press
   - 30-second timeout from grind start to completion
   - Noise-adaptive settling detection

2. **Grinder Saturation Phase** (Weight mode only)
   - Saturates the grinder before main grind for accurate latency detection
   - Configurable amount: 0.1g-5.0g (default 1.0g)
   - **Prime mode**: Keeps coffee, continues immediately after settling
   - **Purge mode**: Shows confirmation popup, waits for user to discard stale grinds
   - Logging and chart updates disabled during purge confirmation

3. **Predictive Phase**
   - Learns flow rate and motor-to-cup latency (relay + motor inertia + burr spin-up)
   - Predicts when to stop motor based on measured flow and coast characteristics
   - Target: barely undershoot target weight (overshoot is unrecoverable)
   - Uses runtime-configurable motor response latency (30-200ms, default 50ms)

4. **Pulse Correction Phase**
   - Used by the default **Precision** finish mode
   - Conservative pulse duration calculation using 95th percentile flow rate
   - Bounded pulses respect hardware-specific motor response latency
   - Pulses range from motor latency minimum to latency + 225ms maximum
   - Mechanical instability detection (3+ sudden weight drops triggers diagnostic)
   - Repeats until target ± tolerance reached

   With **Predictive / pulse-free** selected, the same live predictive stop is
   used, but the controller completes after the first settled motor stop instead
   of entering pulse correction. This avoids a motor restart at the cost of a
   greater chance of a small underdose or overshoot.

5. **Time Mode Additional Pulses**
   - Dedicated `TIME_ADDITIONAL_PULSE` phase for post-completion grinding
   - 100ms fixed pulse duration
   - Split-button UI: OK + PULSE buttons on completion screen

**Motor Response Latency Model:**

The motor response latency represents the physical system lag between relay activation and grounds production. This value is hardware-specific and accounts for:
- Relay closure time (solid-state vs mechanical relays)
- Motor inertia (110V vs 220V motors)
- Burr spin-up characteristics (different grinder models/designs)

The latency value is automatically calibrated via **Auto-Tune Motor Response** (Menu → Tune Pulses) using binary search with statistical verification, or uses a safe 50ms default. This enables universal grinder compatibility without firmware modifications.

**Key Features:**
- Noise-resistant through multi-modal load cell measurement (instant, smoothed, filtered)
- Hardware-adaptive pulse control via runtime motor latency
- Conservative approach: undershoots target, then corrects with bounded pulses
- Optional single-run Predictive finish using the same flow and coast model
- Mechanical instability detection with hysteresis and persistence
- 30-second grind timeout protection with user acknowledgment requirement

---

## Frequently asked questions

**Will this modification work on grinders other than the Eureka Mignon Specialita?**

See the comprehensive **[Grinder Compatibility Matrix](GRINDER_COMPATIBILITY.md)** for detailed compatibility information across different grinder models, including confirmed compatible models, adaptation requirements, and installation methods.

**Can I use this to grind directly into a portafilter instead of a dosing cup?**

Yes, but requires modifications: use 1kg load cell (vs 0.3kg) for better accuracy with heavier portafilters. Design and 3D print custom portafilter holder mounting to load cell. The dosing cup holder design serves as reference for portafilter adapter.

---

## Troubleshooting

For common build and setup issues, see **[TROUBLESHOOTING.md](TROUBLESHOOTING.md)**.

---

For additional support, use the
[community fork's issue tracker](https://github.com/Clinteastman/smart-grind-by-weight/issues)
and include the board generation, firmware version and a diagnostic log where
possible.

---

**Previous:** [Diagnostics and data](DIAGNOSTICS_AND_DATA.md) ·
**Back to:** [Documentation home](DOC.md)
