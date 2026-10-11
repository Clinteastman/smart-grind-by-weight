## Project Overview

ESP32-S3 intelligent coffee scale with grind-by-weight functionality. Features predictive grinding system, LVGL touch UI, and BLE OTA updates. Automatically grinds coffee beans to precise target weights using flow prediction and pulse correction algorithms.

The firmware is a native **ESP-IDF** application, built with ESP-IDF v6.1's
own tooling (`idf.py`, CMake and Ninja). There is no Arduino core: the entry
point is `app_main()` in `src/main.cpp`, and every peripheral, storage and
network API is an ESP-IDF one.

## Essential Commands

The canonical checkout is `/home/cmossom/src/smart-grind-by-weight` on the
native WSL2 ext4 filesystem in `Ubuntu-24.04`. Do not build this project from a
Windows checkout, OneDrive, `/mnt/c`, or another Windows-mounted path. From
PowerShell, enter the canonical checkout with:

```powershell
wsl.exe -d Ubuntu-24.04 --cd /home/cmossom/src/smart-grind-by-weight
```

Building, flashing and monitoring need ESP-IDF v6.1 activated in the shell
(`source ~/.espressif/tools/activate_idf_v6.1.sh` for an EIM install, or
`. ~/esp/esp-idf/export.sh` for a git install; `idf.py --version` should print
ESP-IDF v6.1). See `docs/DEVELOPMENT.md` for installation.

All development tasks use the unified cross-platform Python tool:

```bash
# Build and upload
python3 tools/grinder.py build-upload

# Data analysis (exports data and launches Streamlit report)  
python3 tools/grinder.py analyze
```

**Common Commands:**
- `python3 tools/grinder.py build --hardware v1 --jobs 8` - Build V1 firmware
- `python3 tools/grinder.py build --hardware v2 --jobs 8` - Build V2 firmware
- `python3 tools/grinder.py build --hardware debug|mock` - Build the V1 debug or mock firmware
- `python3 tools/grinder.py upload` - Upload latest firmware via BLE
- `python3 tools/grinder.py flash-usb --hardware v1|v2 --port <port>` - Flash the latest archived image over USB, keeping settings and data
- `idf.py -B build/v2 -p <port> flash` - Full USB flash of a board (bootloader, partition table, OTA data, app)
- `idf.py -B build/<variant> -p <port> monitor` - Serial monitor with backtrace decoding (exit with Ctrl+])
- `python3 tools/grinder.py export` - Export grind data to database
- `python3 tools/grinder.py report` - Launch Streamlit report from existing data
- `python3 tools/grinder.py scan` - Scan for BLE devices
- `python3 tools/grinder.py info` - Get device system information
- `python3 tools/grinder.py clean` - Remove every variant's build directory

A build prints its build number and writes
`build/<variant>/smart-grind-by-weight.bin`; V1 and V2 images are also archived
as `firmware_cache/waveshare-164-v1/build_NNN.bin` and
`firmware_cache/waveshare-164-v2/build_NNN.bin`.

## Architecture

**Layered Architecture:**
1. **Hardware Layer** (`src/hardware/`): ESP32-S3 peripheral abstraction
2. **Control Layer** (`src/controllers/`): Business logic and algorithms  
3. **System Layer** (`src/system/`): State management, timing, chip info
4. **Storage Layer** (`src/storage/`): NVS settings store and LittleFS access
5. **Network Layer** (`src/network/`): Wi-Fi, HTTP server, JSON API, provisioning
6. **UI Layer** (`src/ui/`): LVGL touchscreen interface

**ESP-IDF build layout:**
- `CMakeLists.txt` (root) registers `src/` as the application component
  (`EXTRA_COMPONENT_DIRS`) and builds only it and its dependencies
  (`COMPONENTS src`). It also adds `include/` to every component's search path
  so LVGL picks up `lv_conf.h`. LVGL's Kconfig otherwise ignores it, which
  silently drops the fonts the UI needs (`CONFIG_LV_CONF_SKIP=n`).
- `src/CMakeLists.txt` registers the firmware sources and runs
  `tools/build-scripts/build_info.py` before every build to write
  `src/config/git_info.h` (build number and git commit).
- `src/idf_component.yml` pins the managed dependencies: LVGL, littlefs,
  esp-nimble-cpp, esp_delta_ota, mdns, esp_websocket_client, esp_lcd_co5300,
  esp_lcd_sh8601, improv. They download to `managed_components/`.
- `sdkconfig.defaults` carries CPU, PSRAM, partition, MAC address, Wi-Fi, BLE,
  TLS and FreeRTOS settings shared by every variant. `CONFIG_FREERTOS_HZ=1000`
  is required: the control loops run on 20/25/50 ms periods and would otherwise
  quantise to 10 ms steps.
- Four variants - `v1`, `v2`, `debug`, `mock` - each build in `build/<variant>/`
  with their own `build/<variant>/sdkconfig`, generated from
  `sdkconfig.defaults` plus an overlay: `sdkconfig.defaults.v2` (V2 board),
  `sdkconfig.defaults.debug` (V1, debug optimisation, 2 s startup pause) or
  `sdkconfig.defaults.mock` (V1, simulated load cell and motor, background
  indicator). `v1` uses `sdkconfig.defaults` alone.
- Board and debug switches are Kconfig options in the "Smart Grind" menu
  (`src/Kconfig.projbuild`): board revision V1/V2, simulated load cell and
  motor, background tint while the motor runs, startup pause.
  `src/config/hardware.h` and `src/config/debug.h` map them onto
  `HW_DISPLAY_VARIANT_V2`, `DEBUG_ENABLE_LOADCELL_MOCK`,
  `DEBUG_ENABLE_GRINDER_BACKGROUND_INDICATOR` and `UI_DEBUG_SERIAL_DELAY_MS`.
  There are no `-D` build flags.
- Kconfig changes go in `sdkconfig.defaults` (every variant) or a variant's
  overlay. An existing `build/<variant>/sdkconfig` is reused as is: delete it,
  or run `python3 tools/grinder.py clean`, to regenerate it after editing a
  defaults file. `idf.py -B build/<variant> menuconfig` edits an already-built
  variant's sdkconfig (options under "Smart Grind").

**Key native replacements** (no Arduino compatibility layer):
- Display: `esp_lcd` for both revisions - `esp_lcd_co5300` on V1,
  `esp_lcd_sh8601` on V2, both from the ESP Component Registry. Same QSPI bus,
  40 MHz, `esp_lcd_panel_set_gap` for the panel's 20-pixel column offset.
- BLE: `esp-nimble-cpp` (`NimBLEDevice`/`NimBLEServer`/`NimBLECharacteristic`).
- HTTP: `esp_http_server` behind `src/network/http_support.h`, which adapts
  capturing lambdas into route handlers and provides request/response helpers.
  `src/network/http_multipart.h` streams the firmware and screensaver uploads.
- Settings: `Preferences` in `src/storage/preferences.h` over NVS. Its on-flash
  encoding must not change - an existing grinder would lose calibration and
  history.
- Filesystem: `filesystem`/`FsFile` in `src/storage/filesystem.h` over
  `esp_littlefs`, mounted at `/littlefs` from the `spiffs` partition.
- Time: `millis()`/`micros()` from `src/system/timing.h` over `esp_timer`.
  Use `vTaskDelay(pdMS_TO_TICKS(x))` for delays.
- Strings: `std::string` plus the helpers in `src/system/string_utils.h`.

**Key Components:**
- **HardwareManager**: Central hardware coordinator
- **GrindController**: Multi-phase state machine with predictive flow control, 10 pulse corrections, mechanical instability detection, time mode additional pulses, and target-free manual grinding
- **LoadCell (HX711)**: Multi-mode precision weight measurement (instant, smoothed, filtered), calibration flag, noise diagnostics
- **DiagnosticsController**: System health monitoring (calibration status, sustained noise, mechanical instability), state persistence, hysteresis, priority-based warnings
- **UIManager**: 7 screens with LVGL integration; menu page surfaces quick Tools (Scale view, Calibrate, Tune Pulses, Motor Test) followed by Settings (Bluetooth, Display, Grind Settings) and Info sections (Diagnostics, System Info, Logs & Data, Lifetime Stats), warning icon indicator, split-button layout for time mode pulses
- **StateMachine**: Central state coordination (READY → GRINDING → GRIND_COMPLETE)

**Update Intervals:** 20ms grind control, 25ms load cell (active), 50ms UI/hardware

**Grind Phases:**
- Standard phases: IDLE, INITIALIZING, SETUP, TARING, TARE_CONFIRM, PRIME, PRIME_SETTLING, PREDICTIVE, PULSE_DECISION, PULSE_EXECUTE, PULSE_SETTLING, FINAL_SETTLING, TIME_GRINDING, MANUAL_GRINDING, COMPLETED, TIMEOUT
- `TIME_ADDITIONAL_PULSE` - Dedicated phase for post-completion additional grinding pulses in time mode
- `PURGE_CONFIRM` - Pauses after chute operation (in Purge mode) to allow user to discard grinds before continuing to main grind
- **Timeouts**: Targeted grinds stop after 60 seconds; target-free Manual mode has its own 30-second safety cutoff

**Grinder Purge/Prime:**
- **Always runs** before weight-mode grinding to saturate the grinder for accurate latency detection
- **Prime mode**: Keeps coffee, continues immediately to PREDICTIVE phase
- **Purge mode** (default): Shows confirmation popup, waits for user to discard stale grinds, then continues
- **Configurable amount**: 0.1g-5.0g (default 1.0g), replaces old hardcoded `GRIND_PRIME_TARGET_WEIGHT_G`
- **Purge popup**: "Keep purge grinds from now on" checkbox switches mode from Purge → Prime in preferences
- **Logging disabled** during PURGE_CONFIRM phase to avoid capturing data while paused
- **Preferences**: `chute_mode` (int: 0=Prime, 1=Purge, default=1), `chute_amount_g` (float: 0.1-5.0, default=1.0)

**Time Mode Pulses:** Split-button completion screen (OK + PULSE), `TIME_ADDITIONAL_PULSE` phase, 100ms duration

**Grind Settings:** Configurable through Menu → Grind Settings page
- **Mode Selection**: Radio buttons for Weight/Time mode selection
- **Swipe Gestures Toggle**: Enable/disable vertical swipe gestures for mode switching (default: disabled)
- **Automation**: Start on Cup and Return on Removal toggles
- **Purging**: Radio buttons (Prime/Purge) and Amount slider (0.1g-5.0g)
- **Preferences**: `swipe.enabled` (boolean), `grind_mode` (0=Weight, 1=Time), `chute_mode` (0=Prime, 1=Purge), `chute_amount_g` (float)
- **Behavior**: Swipe gestures only work when enabled; direct mode selection always works

**Color Scheme (RGB565):**
- `COLOR_PRIMARY`: 0xFF0000 (Red) - Primary theme color
- `COLOR_ACCENT`: 0x00AAFF (Blue) - Highlights and accents
- `COLOR_SUCCESS`: 0x00AA00 (Green) - Success states
- `COLOR_WARNING`: 0xCC8800 (Orange) - Warning states
- `COLOR_BACKGROUND`: 0x000000 (Black) - Background
- `COLOR_TEXT_PRIMARY`: 0xFFFFFF (White) - Primary text

**Font Usage Hierarchy:**
- `lv_font_montserrat_24`: Standard text and button labels
- `lv_font_montserrat_32`: Button symbols (OK, CLOSE, PLUS, MINUS)
- `lv_font_montserrat_36`: Screen titles
- `lv_font_montserrat_56`: Large weight displays

## Development Notes

* When modifying this codebase, follow the existing architectural patterns, maintain the clean separation between layers, and ensure any timing-critical code respects the established update intervals.
* use macos compatible commands. macos uses python3
* after making a test build let me know the build number
* Always read entire files. Otherwise, you don’t know what you don’t know, and will end up making mistakes, duplicating code that already exists, or misunderstanding the architecture.  
* Commit early and often. When working on large tasks, your task could be broken down into multiple logical milestones. After a certain milestone is completed and confirmed to be ok by the user, you should commit it. If you do not, if something goes wrong in further steps, we would need to end up throwing away all the code, which is expensive and time consuming.  
* Your internal knowledgebase of libraries might not be up to date. When working with any external library, unless you are 100% sure that the library has a super stable interface, you will look up the latest syntax and usage via either Perplexity (first preference) or web search (less preferred, only use if Perplexity is not available)  
* Do not say things like: “x library isn’t working so I will skip it”. Generally, it isn’t working because you are using the incorrect syntax or patterns. This applies doubly when the user has explicitly asked you to use a specific library, if the user wanted to use another library they wouldn’t have asked you to use a specific one in the first place.  
* Always run linting after making major changes. Otherwise, you won’t know if you’ve corrupted a file or made syntax errors, or are using the wrong methods, or using methods in the wrong way.   
* Please organise code into separate files wherever appropriate, and follow general coding best practices about variable naming, modularity, function complexity, file sizes, commenting, etc.  
* Code is read more often than it is written, make sure your code is always optimised for readability  
* Unless explicitly asked otherwise, the user never wants you to do a “dummy” implementation of any given task. Never do an implementation where you tell the user: “This is how it *would* look like”. Just implement the thing.  
* Whenever you are starting a new task, it is of utmost importance that you have clarity about the task. You should ask the user follow up questions if you do not, rather than making incorrect assumptions.  
* Do not carry out large refactors unless explicitly instructed to do so.  
* When starting on a new task, you should first understand the current architecture, identify the files you will need to modify, and come up with a Plan. In the Plan, you will think through architectural aspects related to the changes you will be making, consider edge cases, and identify the best approach for the given task. Get your Plan approved by the user before writing a single line of code.   
* If you are running into repeated issues with a given task, figure out the root cause instead of throwing random things at the wall and seeing what sticks, or throwing in the towel by saying “I’ll just use another library / do a dummy implementation”.   
* You are an incredibly talented and experienced polyglot with decades of experience in diverse areas such as software architecture, system design, development, UI & UX, copywriting, and more.  
* When doing UI & UX work, make sure your designs are both aesthetically pleasing, easy to use, and follow UI / UX best practices. You pay attention to interaction patterns, micro-interactions, and are proactive about creating smooth, engaging user interfaces that delight users.   
* When you receive a task that is very large in scope or too vague, you will first try to break it down into smaller subtasks. If that feels difficult or still leaves you with too many open questions, push back to the user and ask them to consider breaking down the task for you, or guide them through that process. This is important because the larger the task, the more likely it is that things go wrong, wasting time and energy for everyone involved.
- Touch polling uses the IDF I2C master driver with ACK checking disabled so idle NACKs don't spam logs. Toggle `DEBUG_SUPPRESS_TOUCH_I2C_ERRORS` to 0 if you need the raw driver output for troubleshooting.
- Improv serial provisioning installs the USB-Serial-JTAG driver and points stdio at it, so console output and Improv frames share one ordered stream.
- Use the src/config/constants.h aggregation file to include constants / settings - dont refer to config files directly.
- When new features have been added and tested always update the docs as well
- when making a commit, only focus on the end result not the process we went through to get to the end result
