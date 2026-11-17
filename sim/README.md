# Desktop simulator

The desktop simulator runs the Smart Grind-by-Weight LVGL interface in a native
desktop window on Windows, macOS or Linux. It is intended for fast UI and
grind-flow development without flashing or connecting a development board.

The simulated panel has the same 280 x 456 logical resolution as the Waveshare
display. It currently runs the production Ready screen, the three production
Grinding screen layouts (arc, chart and circle), and the production
Play/Stop/Complete control. A
deterministic grinder/load-cell model supplies rising weight, changing flow,
motor run-on, settling, and completion data.

Windows uses LVGL's native Win32 driver. macOS and Linux use LVGL's SDL2
driver. Only `platform/sim_platform_*.cpp` differs between them; the screens,
grind model and tests are shared.

## Requirements

Windows:

- Windows 10 or later
- Visual Studio 2022 with the Desktop development with C++ workload
- Git and internet access for the first standalone build (CMake fetches LVGL 9.5.0)

No ESP32, display, load cell, grinder, PlatformIO, or SDL installation is
required.

macOS:

- Xcode Command Line Tools (`xcode-select --install`)
- CMake 3.24 or later and SDL2: `brew install cmake sdl2`

Linux:

- A C/C++ compiler, CMake 3.24 or later and the SDL2 development package
  (for example `sudo apt install build-essential cmake libsdl2-dev`)

## Run

Windows, from PowerShell at the repository root:

```powershell
.\sim\run.ps1
```

macOS or Linux, from the repository root:

```bash
sim/run.sh
```

If a firmware build has already installed LVGL 9.5.0, the simulator reuses that
source and performs no second download. Otherwise the first run downloads LVGL;
later runs reuse the local build. Set `SMART_GRIND_LVGL_SOURCE` to an existing
LVGL 9.5.0 source directory when using a custom dependency layout.

Use the on-screen circular button to start, stop, acknowledge, and restart the
grind flow. The desktop-only keyboard shortcuts are kept outside the simulated
panel UI:

- `V`: cycle through the production arc, chart and circle grinding layouts
- `T`: tare the simulated load cell

## Automated smoke test

Windows:

```powershell
.\sim\build.ps1 -Test
```

macOS or Linux:

```bash
sim/run.sh --test
```

The smoke scenario creates the production UI, starts a grind, verifies that the
screen transitions to Grinding, and confirms that simulated load-cell weight
advances. A circle scenario runs a complete grind on the circle layout and
checks that the circle grows with progress, then that the display turns
solid green.

The test command also runs deterministic render-budget benchmarks for the arc
and chart layouts and for an animated Ready-screen tab swipe. They count LVGL
flushes and flushed pixels, protecting the UI from accidentally returning to
excessive redraws or sluggish page transitions. The swipe budget is measured
per frame, so it does not depend on how many frames the host computer renders.

The simulator does not execute built ESP32 `.bin` files; those contain Xtensa
machine code and cannot run in a desktop process. The complete compatibility
gate is therefore:

1. Build the V1 firmware target.
2. Build the V2 firmware target.
3. Build and run this simulator smoke test against the shared production UI
   source.

## Scope and hardware boundary

The simulator is suitable for UI layout, interaction flows, deterministic grind
scenarios, and future web/BLE integration work. Physical hardware remains the
authority for AMOLED initialization, QSPI/DMA timing, real touch-controller
behaviour, ESP32 memory pressure, BLE/Wi-Fi coexistence, relay wiring, HX711
electrical noise, and final motor safety checks. A desktop computer renders
far faster than the ESP32, so the simulator cannot show the device's frame
rate; use its redrawn-pixel counts as a proxy and confirm smoothness on
hardware.
