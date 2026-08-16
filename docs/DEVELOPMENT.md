# Development Guide

This guide is for developers who want to build the Smart Grind-by-Weight firmware from source, contribute to the project, or modify the code for their own use.

**End users:** If you just want to use the device, download pre-built firmware from [Community Releases](https://github.com/Clinteastman/smart-grind-by-weight/releases) instead.

---

## 🛠️ Development Setup

### Canonical WSL2 environment

The maintained development checkout is
`/home/cmossom/src/smart-grind-by-weight` in the `Ubuntu-24.04` WSL2
distribution. Keep the repository, PlatformIO working files and compiler object
caches on WSL2's native ext4 filesystem. Do not build from OneDrive, `/mnt/c`,
or a Windows checkout: Linux tools accessing Windows-mounted files pay a large
per-file overhead, which is particularly costly for PlatformIO and LVGL.

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

- **Python 3.8+** with pip
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

2. **Install development dependencies:**
   ```bash
   python3 tools/grinder.py install
   ```

This automatically creates a virtual environment and installs all required dependencies including PlatformIO.

---

## 🖥️ Desktop Simulator

On Windows, the native simulator runs the production LVGL Ready and Grinding
screens at the real display resolution, with mouse input and a deterministic
grinder/load-cell scenario. It requires Visual Studio 2022 with the Desktop
development with C++ workload, but no ESP32, display, load cell, PlatformIO, or
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

## 🔧 Firmware Build Targets

The project has four build targets:

### Production Target: `waveshare-esp32s3-touch-amoled-164`
- **Use case:** Real V1 hardware with load cell and grinder connected
- **Hardware:** Full ESP32-S3 + HX711 + load cell + grinder motor relay
- **Features:** All functionality enabled
- **Optimizations:** `-Ofast` optimization level for performance

### V2 Production Target: `waveshare-esp32s3-touch-amoled-164-v2`
- **Use case:** Waveshare 1.64-inch V2 hardware with load cell and grinder connected
- **Display:** SH8601 using Waveshare's native `esp_lcd` QSPI driver
- **External GPIO:** HX711 SCK on GPIO 1; grinder motor control on GPIO 16 (GPIO 18 is reserved by `TP_INT`)
- **Important:** V1 and V2 display firmware is not interchangeable; the wrong target normally boots to a black screen

### Debug Target: `waveshare-esp32s3-touch-amoled-164-debug`
- **Use case:** Development and debugging with real hardware
- **Hardware:** Full ESP32-S3 + HX711 + load cell + grinder motor relay
- **Features:**
  - All functionality enabled
  - Debug symbols included
  - 2-second UI serial delay for easier debugging
  - Serial monitor filters to suppress harmless touch driver errors

### Mock/Development Target: `waveshare-esp32s3-touch-amoled-164-mock`
- **Use cases:**
  - Development without connected load cell or grinder
  - Testing with device installed in grinder without wasting beans or taxing the motor
- **Hardware:** Can run on just the ESP32-S3 Waveshare board (without HX711 or grinder) OR with full hardware installed
- **Features:**
  - Simulated load cell readings (green background indicates mock HX711 driver is active)
  - Mock grinder motor (visual indicator instead of relay activation)
  - Debug features enabled

**Mock mode benefits:**
- Develop UI changes without affecting the actual grinder
- Bring your waveshare board with you for coding and testing on the road :)
- Work on new features without hardware setup or bean waste
- Capture USB serial messages for debugging

---

## 🚀 Building & Flashing

### Development Platform

This project uses the **pioarduino ESP32 platform** (a community fork) instead of the standard Espressif ESP32 platform. This ensures proper support for the Waveshare ESP32-S3 AMOLED display.

**Platform Details:**
- **Platform**: [pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32) (stable release)
- **Framework**: Arduino ESP32 Core 3.x
- **Target**: ESP32-S3 with AMOLED touch display

The platform dependency is automatically handled by PlatformIO via the `platformio.ini` configuration.

### Build Commands

**Build production firmware:**
```bash
tools/venv/bin/python3 tools/grinder.py build --hardware v1 --jobs 8
# Equivalent: platformio run -e waveshare-esp32s3-touch-amoled-164
```

The grinder tool keeps PlatformIO's compiled-object cache in the operating
system's user cache directory, with separate subdirectories for the V1 and V2
targets. Compatible objects can therefore be reused across Git worktrees and
branches without mixing board-specific LVGL objects. Set
`SMART_GRIND_BUILD_CACHE_DIR` to choose a different cache root, or
`PLATFORMIO_BUILD_CACHE_DIR` when invoking PlatformIO directly.

**Build V2 production firmware:**
```bash
tools/venv/bin/python3 tools/grinder.py build --hardware v2 --jobs 8
```

Archived local V1 builds are stored in `firmware_cache/`; incompatible V2
builds are stored separately in `firmware_cache/waveshare-164-v2/`.

Build and flash operations use a project lock, so a second compiler or uploader
cannot silently start against the same working tree. If a process terminates
unexpectedly, the next command checks whether the recorded process is still
alive before treating the lock as stale.

**Build debug firmware:**
```bash
python3 tools/venv/bin/python -m platformio run -e waveshare-esp32s3-touch-amoled-164-debug
```

**Build mock/development firmware:**
```bash
python3 tools/venv/bin/python -m platformio run -e waveshare-esp32s3-touch-amoled-164-mock
```

**Clean build artifacts:**
```bash
python3 tools/grinder.py clean
```

### Initial USB Flashing

For the first-time setup or when BLE isn't working:

```bash
# Build and upload via USB (production)
python3 tools/grinder.py build
python3 tools/venv/bin/python -m platformio run --target upload -e waveshare-esp32s3-touch-amoled-164

# Or for the V2 hardware revision
python3 tools/venv/bin/python -m platformio run --target upload -e waveshare-esp32s3-touch-amoled-164-v2

# Or for debug target
python3 tools/venv/bin/python -m platformio run --target upload -e waveshare-esp32s3-touch-amoled-164-debug

# Or for mock target
python3 tools/venv/bin/python -m platformio run --target upload -e waveshare-esp32s3-touch-amoled-164-mock
```

To reinstall an already archived application image without rebuilding or
erasing Wi-Fi credentials, settings, grind history or screensavers, use:

```bash
python3 tools/grinder.py flash-usb --hardware v1 --port COM15
python3 tools/grinder.py flash-usb --hardware v2 --port COM15
```

The tool reads the board's OTA selection metadata and writes the application
partition that the bootloader is currently using. It never clears OTA metadata
to force a slot; doing that can make an otherwise healthy board fall back to an
old factory application.

### BLE OTA Updates (After Initial Setup)

Once the device is running and connected to Bluetooth:

```bash
# Build and upload wirelessly (production)
python3 tools/grinder.py build-upload

# Upload specific firmware file
python3 tools/grinder.py upload path/to/smart-grind-by-weight-vX.X.X.bin

# Force full firmware update (skip delta patching)
python3 tools/grinder.py build-upload --force-full

# Scan for BLE devices
python3 tools/grinder.py scan

# Get device system info
python3 tools/grinder.py info
```

---

## 📦 Release Process

For maintainers creating releases, see **[RELEASES.md](RELEASES.md)** for detailed release workflow documentation.

---

## 🐛 Debugging

### Serial Monitor

```bash
# Monitor serial output via PlatformIO
python3 tools/venv/bin/python -m platformio device monitor
```

### BLE Debug Monitoring

```bash
# Live debug monitoring via BLE
python3 tools/grinder.py debug
```

**⚠️ BLE Monitoring Limitations:**
- **Boot messages are missed** - BLE connection establishes after device boot
- **Kernel panics not captured** - System-level crashes bypass BLE and go directly to serial
- **Framework messages missing** - Low-level Arduino/ESP-IDF messages don't route through BLE
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
