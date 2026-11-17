# Development Guide

This guide is for developers who want to build the Smart Grind-by-Weight firmware from source, contribute to the project, or modify the code for their own use.

**End users:** If you just want to use the device, download pre-built firmware from [Community Releases](https://github.com/Clinteastman/smart-grind-by-weight/releases) instead.

---

## 🛠️ Development Setup

### Canonical WSL2 environment

The maintained development checkout is
`/home/cmossom/src/smart-grind-by-weight` in the `Ubuntu-24.04` WSL2
distribution. Keep the repository, the ESP-IDF installation and the `build/`
directories on WSL2's native ext4 filesystem. Do not build from OneDrive,
`/mnt/c`, or a Windows checkout: Linux tools accessing Windows-mounted files pay
a large per-file overhead, which is particularly costly for the ESP-IDF build
and LVGL.

The measured clean dual-board build comparison on this project was about 26
minutes 37 seconds from the Windows filesystem versus 75-80 seconds from native
WSL2 storage. This is why the WSL2 path is the source of truth, not merely an
optional optimization. This follows Microsoft's guidance to keep files in the
WSL filesystem when Linux command-line tools do the work:
[Working across file systems](https://learn.microsoft.com/en-us/windows/wsl/filesystems#file-storage-and-performance-across-file-systems).

From PowerShell, open a WSL shell in the canonical checkout with:

```powershell
wsl.exe -d Ubuntu-24.04 --cd /home/cmossom/src/smart-grind-by-weight
```

Run firmware work inside that shell. The native Windows desktop simulator is
the only build-tool exception; it still uses the same WSL checkout as its source
of truth.

### Firmware prerequisites

- **ESP-IDF v6.1** for building and flashing the firmware; see
  [ESP-IDF Toolchain](#esp-idf-toolchain)
- **Python 3.10+** with pip, the oldest Python ESP-IDF v6.1 supports
- **Git** for version control
- **USB cable** for initial firmware flashing
- **Hardware** (ESP32-S3 board, HX711, load cell) for testing

Hardware is not required for desktop UI and simulated grind-flow development;
see [Desktop Simulator](#desktop-simulator).

### Initial Setup

1. **Clone the repository:**
   ```bash
   git clone https://github.com/Clinteastman/smart-grind-by-weight.git
   cd smart-grind-by-weight
   ```

2. **Install and activate ESP-IDF v6.1** as described in
   [ESP-IDF Toolchain](#esp-idf-toolchain).

3. **Install the Python tools:**
   ```bash
   python3 tools/grinder.py install
   ```

This creates the `tools/venv` virtual environment and installs the Bluetooth,
data-analysis and USB-flashing tools (including esptool). Building the firmware
needs the activated ESP-IDF environment, not this virtual environment.

---

## 🖥️ Desktop Simulator

On Windows, the native simulator runs the production LVGL Ready and Grinding
screens at the real display resolution, with mouse input and a deterministic
grinder/load-cell scenario. It requires Visual Studio 2022 with the Desktop
development with C++ workload, but no ESP32, display, load cell, ESP-IDF, or
SDL installation.

```powershell
.\sim\run.ps1
```

Run its automated UI/grind smoke scenario with:

```powershell
.\sim\build.ps1 -Test
```

See [sim/README.md](../sim/README.md) for controls, capabilities, and the
hardware-validation boundary.

---

## 🔧 Firmware Variants

The project has four firmware variants. Each builds in its own `build/<variant>/`
directory with its own `build/<variant>/sdkconfig`, generated from
`sdkconfig.defaults` plus the variant's overlay:

| Variant | Board | Configuration |
| --- | --- | --- |
| `v1` | V1 | `sdkconfig.defaults` |
| `v2` | V2 | `sdkconfig.defaults` + `sdkconfig.defaults.v2` |
| `debug` | V1 | `sdkconfig.defaults` + `sdkconfig.defaults.debug` |
| `mock` | V1 | `sdkconfig.defaults` + `sdkconfig.defaults.mock` |

### V1 Production: `v1`
- **Use case:** Real V1 hardware with load cell and grinder connected
- **Hardware:** Full ESP32-S3 + HX711 + load cell + grinder motor relay
- **Features:** All functionality enabled
- **Optimizations:** Optimized for speed (`CONFIG_COMPILER_OPTIMIZATION_PERF`, `-O2`), like every ESP-IDF component

### V2 Production: `v2`
- **Use case:** Waveshare 1.64-inch V2 hardware with load cell and grinder connected
- **Display:** SH8601 using Waveshare's native `esp_lcd` QSPI driver
- **External GPIO:** HX711 SCK on GPIO 1; grinder motor control on GPIO 16 (GPIO 18 is reserved by `TP_INT`)
- **Important:** V1 and V2 display firmware is not interchangeable; the wrong variant normally boots to a black screen

### Debug: `debug`
- **Use case:** Development and debugging with real V1 hardware
- **Hardware:** Full ESP32-S3 + HX711 + load cell + grinder motor relay
- **Features:**
  - All functionality enabled
  - Optimized for debugging (`CONFIG_COMPILER_OPTIMIZATION_DEBUG`)
  - 2-second pause at startup so a serial monitor can attach

### Mock/Development: `mock`
- **Use cases:**
  - Development without connected load cell or grinder
  - Testing with device installed in grinder without wasting beans or taxing the motor
- **Hardware:** Can run on just the ESP32-S3 Waveshare V1 board (without HX711 or grinder) OR with full hardware installed
- **Features:**
  - Simulated load cell readings (green background indicates mock HX711 driver is active)
  - Mock grinder motor (visual indicator instead of relay activation)
  - Optimized for debugging (`CONFIG_COMPILER_OPTIMIZATION_DEBUG`)

**Mock mode benefits:**
- Develop UI changes without affecting the actual grinder
- Bring your waveshare board with you for coding and testing on the road :)
- Work on new features without hardware setup or bean waste
- Capture USB serial messages for debugging

The board revision and the mock, background-indicator and startup-pause
switches are options in the **Smart Grind** Kconfig menu, defined in
`src/Kconfig.projbuild`. The overlays select them; there are no compiler `-D`
flags to set.

---

## 🚀 Building & Flashing

### ESP-IDF Toolchain

The firmware is a native **ESP-IDF** application and builds with ESP-IDF's own
tooling: `idf.py`, CMake and Ninja.

**Toolchain Details:**
- **Framework**: ESP-IDF v6.1
- **Target**: ESP32-S3 with AMOLED touch display

**Install ESP-IDF v6.1** once, using either:
- Espressif's [ESP-IDF Installation Manager (EIM)](https://docs.espressif.com/projects/idf-im-ui/en/latest/)
  (recommended), selecting version v6.1; or
- a git checkout, following the
  [ESP-IDF v6.1 Get Started guide](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/index.html):
  ```bash
  mkdir -p ~/esp
  git clone --recursive -b v6.1 https://github.com/espressif/esp-idf.git ~/esp/esp-idf
  cd ~/esp/esp-idf && ./install.sh esp32s3
  ```

**Activate ESP-IDF** in each new shell before building, flashing or monitoring:

```bash
# EIM installation (Linux/macOS)
source ~/.espressif/tools/activate_idf_v6.1.sh

# Or a git installation
. ~/esp/esp-idf/export.sh

# Check the active version: prints ESP-IDF v6.1
idf.py --version
```

On Windows, use the ESP-IDF PowerShell environment that EIM installs.

On the first build, the ESP-IDF component manager downloads the managed
dependencies pinned in `src/idf_component.yml` into `managed_components/`.

**Project layout:**
- `CMakeLists.txt` - project root; registers `src/` as the application
  component and builds only it and its dependencies, and puts `include/` on
  every component's search path so LVGL finds `lv_conf.h`
- `src/CMakeLists.txt` - the firmware component; runs
  `tools/build-scripts/build_info.py` before every build to write
  `src/config/git_info.h`
- `src/Kconfig.projbuild` - the **Smart Grind** menu: board revision (V1/V2),
  simulated load cell and motor, screen tint while the motor runs, and the
  startup pause
- `src/idf_component.yml` - managed dependencies (LVGL, littlefs, NimBLE-C++,
  esp_delta_ota, mdns, esp_websocket_client, esp_lcd_co5300, esp_lcd_sh8601,
  improv)
- `sdkconfig.defaults` - CPU, PSRAM, partitions, BLE, TLS and FreeRTOS settings
  shared by every variant
- `sdkconfig.defaults.v2`, `sdkconfig.defaults.debug` and
  `sdkconfig.defaults.mock` - variant overlays layered on `sdkconfig.defaults`

### Configuration

A variant's `build/<variant>/sdkconfig` is generated on its first build and
reused as is afterwards. After editing `sdkconfig.defaults` or an overlay,
delete that variant's `build/<variant>/sdkconfig`, or run
`python3 tools/grinder.py clean` to remove every build directory, so the next
build regenerates it.

To explore options interactively, edit the sdkconfig of a variant that has
already been built with:

```bash
idf.py -B build/v2 menuconfig
```

The project's own options are under **Smart Grind**. A regenerated sdkconfig
does not keep menuconfig changes, so move any setting you want to keep into
`sdkconfig.defaults` or the variant's overlay.

### Build Commands

Run these in a shell with ESP-IDF activated.

**Build production firmware:**
```bash
python3 tools/grinder.py build --hardware v1 --jobs 8
```

**Build V2 production firmware:**
```bash
python3 tools/grinder.py build --hardware v2 --jobs 8
```

**Build debug firmware:**
```bash
python3 tools/grinder.py build --hardware debug
```

**Build mock/development firmware:**
```bash
python3 tools/grinder.py build --hardware mock
```

`--jobs` sets the number of parallel compiler jobs (default: at most 8). Each
build produces `build/<variant>/smart-grind-by-weight.bin`, plus
`build/<variant>/bootloader/bootloader.bin` and
`build/<variant>/partition_table/partition-table.bin`, and prints the build
number. V1 and V2 images are also archived as
`firmware_cache/waveshare-164-v1/build_NNN.bin` and
`firmware_cache/waveshare-164-v2/build_NNN.bin`. The archives serve as delta
bases for Bluetooth updates and as the images `flash-usb` installs.

The grinder tool runs `idf.py` with the variant's build directory and defaults
files. Run from the repository root, the direct equivalent of the V2 build is:

```bash
idf.py -B build/v2 -D SDKCONFIG=build/v2/sdkconfig \
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.v2" build
```

For V1, use `SDKCONFIG_DEFAULTS=sdkconfig.defaults`. A direct `idf.py` build
does not archive its image in `firmware_cache/`.

`tools/build-scripts/build_info.py` writes the build number and git commit into
`src/config/git_info.h` before every build. Locally the number comes from
`.build_number`; set `SMART_GRIND_INCREMENT_BUILD=1` to advance it.
`SMART_GRIND_BUILD_NUMBER` overrides it, which CI uses so V1 and V2 from one run
report the same number.

Build and flash operations use a project lock, so a second compiler or uploader
cannot silently start against the same working tree. If a process terminates
unexpectedly, the next command checks whether the recorded process is still
alive before treating the lock as stale.

**Clean build artifacts:**
```bash
python3 tools/grinder.py clean
```

This removes every variant's build directory, including its sdkconfig.

### Initial USB Flashing

For the first-time setup or when BLE isn't working, build the matching variant
and flash the bootloader, partition table, OTA data and application with
`idf.py` from the repository root. Replace `<port>` with the board's serial
port, for example `/dev/ttyACM0` on Linux or `COM15` on Windows:

```bash
# Build and flash via USB (production)
python3 tools/grinder.py build --hardware v1
idf.py -B build/v1 -p <port> flash

# Or for the V2 hardware revision
python3 tools/grinder.py build --hardware v2
idf.py -B build/v2 -p <port> flash
```

Debug and mock builds flash the same way from `build/debug` and `build/mock`.
A full flash writes the application to the `factory` slot at 0x20000 and resets
the OTA selection, so the bootloader starts the new image. NVS settings and
LittleFS data are kept.

To reinstall an already archived application image without rebuilding or
erasing Wi-Fi credentials, settings, grind history or screensavers, use:

```bash
python3 tools/grinder.py flash-usb --hardware v1 --port COM15
python3 tools/grinder.py flash-usb --hardware v2 --port COM15
```

The tool reads the board's OTA selection metadata and writes the application
partition that the bootloader is currently using. It never clears OTA metadata
to force a slot; doing that can make an otherwise healthy board fall back to an
old factory application. It uses esptool from the activated ESP-IDF environment,
or from the project virtual environment when ESP-IDF is not active.

### BLE OTA Updates (After Initial Setup)

Once the device is running and connected to Bluetooth:

The delta patch is stored in the `patch` partition and applied with
`esp_delta_ota` once the transfer completes. The rebuilt image streams through
the same `OtaWriter` as a browser upload, so it is checked before any of it
reaches flash: it must be Smart Grind firmware for this chip and flash mode,
and not the build the bootloader last rolled back from.

```bash
# Build and upload wirelessly (production)
python3 tools/grinder.py build-upload

# Or for the V2 hardware revision
python3 tools/grinder.py build-upload --hardware v2

# Upload the most recently built image in build/
python3 tools/grinder.py upload

# Upload specific firmware file
python3 tools/grinder.py upload path/to/smart-grind-by-weight-vX.X.X.bin

# Force full firmware update (skip delta patching)
python3 tools/grinder.py build-upload --force-full

# Scan for BLE devices
python3 tools/grinder.py scan

# Get device system info
python3 tools/grinder.py info
```

`build-upload` builds first, so run it with ESP-IDF activated. The Bluetooth
commands themselves run from the project virtual environment.

When a client connects, the grinder asks for 251-byte link-layer packets (LE
Data Length Extension) and, if the client chose a connection interval slower
than 30 ms, for 15-30 ms. The client may decline either request. The log
records what was agreed, which helps when a transfer is slow or the link
drops:

```
BLE: Link connected - interval 30.00 ms, latency 0, timeout 5000 ms, MTU 23
BLE: MTU 517 agreed
BLE: Link updated - interval 15.00 ms, latency 0, timeout 4000 ms, MTU 517
```

Data export sends each session file in chunks of at most the agreed MTU less
3 bytes, the most one notification can carry. A chunk the Bluetooth stack has
no buffer for is sent again before the next one is read; the export gives up
after about 5 seconds of full buffers.

---

## 📦 Release Process

For maintainers creating releases, see **[RELEASES.md](RELEASES.md)** for detailed release workflow documentation.

---

## 🐛 Debugging

### Serial Monitor

```bash
# Monitor serial output via ESP-IDF (use the variant running on the board)
idf.py -B build/v1 -p <port> monitor
```

The monitor decodes crash backtraces with the ELF file from that build
directory, so point `-B` at the build that is installed. Exit with `Ctrl+]`.
The firmware itself suppresses the harmless touch-controller I2C NACKs from
idle polling (`DEBUG_SUPPRESS_TOUCH_I2C_ERRORS` in `src/config/debug.h`), so no
monitor filter is needed.

### BLE Debug Monitoring

```bash
# Live debug monitoring via BLE
python3 tools/grinder.py debug
```

**⚠️ BLE Monitoring Limitations:**
- **Boot messages are missed** - BLE connection establishes after device boot
- **Kernel panics not captured** - System-level crashes bypass BLE and go directly to serial
- **Framework messages missing** - Low-level ESP-IDF log messages don't route through BLE
- **Best for application debug** - Primarily receives debug messages from the smart-grind-by-weight firmware itself

For complete debugging (including boot sequence and system messages), use USB serial monitoring.

---

## 📚 Additional Documentation

- **[DOC.md](DOC.md)** - Task-oriented documentation home
- **[HARDWARE_INSTALLATION.md](HARDWARE_INSTALLATION.md)** - Parts, wiring and physical installation
- **[FIRMWARE_SETUP.md](FIRMWARE_SETUP.md)** - Firmware selection, flashing and calibration
- **[USER_GUIDE.md](USER_GUIDE.md)** - Touchscreen, web UI and everyday operation
- **[DIAGNOSTICS_AND_DATA.md](DIAGNOSTICS_AND_DATA.md)** - Logs, reports and grind history
- **[TROUBLESHOOTING.md](TROUBLESHOOTING.md)** - Common issues and solutions
- **[GRINDER_COMPATIBILITY.md](GRINDER_COMPATIBILITY.md)** - Adapting to different grinder models
- **[RELEASES.md](RELEASES.md)** - Release process and versioning
