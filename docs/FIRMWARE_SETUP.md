# Firmware, Updates and First Setup

Use this guide to identify the display generation, install or update firmware,
calibrate the load cell and tune the motor response. For physical wiring and
assembly, use [Hardware and installation](HARDWARE_INSTALLATION.md).

**On this page:** [Choose V1 or V2](#check-the-display-revision-first) ·
[Flash in a browser](#web-flasher-recommended) ·
[Command-line fallback](#command-line-fallback) ·
[Initial calibration](#initial-calibration) ·
[Pulse auto-tune](#auto-tune-motor-response)

## Firmware installation

### Check the display revision first

The 1.64-inch Waveshare board now exists in two firmware-incompatible revisions. V1 uses the original CO5300 display path; V2 uses an SH8601 controller, GPIO 46 chip select, and a 20-pixel framebuffer offset. Firmware built for the wrong revision can boot normally while the AMOLED remains completely black.

The PCB silkscreen is not a dependable way to choose between them. The newer SH8601 hardware verified for this project is marked `Rev1.1` on its back but requires the V2 firmware. Here, “V1” and “V2” are convenient names for the original CO5300 and newer SH8601 display generations, not necessarily the revision number printed on the board.

The external wiring also differs: V1 uses GPIO 2 for HX711 SCK and GPIO 18 for motor control; V2 uses GPIO 1 for HX711 SCK and GPIO 16 for motor control. Select the correct firmware target and follow the matching wiring before powering the grinder.

If the board was supplied with V2 factory firmware, or Waveshare's [official V2 demo](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.64-v2) works while V1 firmware stays black, use the V2 build target. See the [black-display troubleshooting entry](TROUBLESHOOTING.md#display-stays-black-after-flashing-waveshare-164-v2) before wiring the grinder.

### Web flasher (recommended)
**[🔗 Open Community Web Flasher Tool](https://clinteastman.github.io/smart-grind-by-weight/)**

Before flashing, verify that the selected image matches the display generation. Choose **Original CO5300 (V1 firmware)** or **Newer SH8601 (V2 firmware; may say Rev1.1)**. If uncertain, use the official-demo test above; flashing the original CO5300 image to SH8601 hardware produces a black screen.

**Browser Compatibility:**
- ✅ **Chrome** (Desktop & Android) - Full support
- ✅ **Microsoft Edge** (Desktop) - Full support
- ❌ **Safari/iOS** - Not supported (use command line method below)
- ❌ **Firefox** - Not supported (use command line method below)

**Two-Step Installation Process:**

1. **Initial Setup (USB - One Time Only)**
   - Connect ESP32 via USB cable
   - Use Chrome/Edge browser (desktop or Android)
   - Select firmware version from dropdown
   - Click "Flash via USB" - opens ESP Web Tools
   - After installation, device is ready for wireless updates

2. **Future Updates (Wi-Fi Recommended)**
   - When the green refresh symbol appears, tap it and confirm **Install**; or
     open **Menu → Wi-Fi** and choose **Install update**
   - Alternatively, open `http://smartgrind.local` (or the IP shown on the
     Wi-Fi page), choose **Settings → System & updates**, and install the latest
     stable release there
   - The grinder chooses the matching V1 or V2 image, validates it, installs it
     and restarts; keep it powered until the update completes
   - Manual `.bin` upload from the
     [Community releases page](https://github.com/Clinteastman/smart-grind-by-weight/releases)
     remains available as an advanced fallback

   BLE updating remains available as a fallback when the grinder cannot join
   the local Wi-Fi network.

**Key Benefits:**
- ✅ **No downloads needed** - firmware hosted automatically
- ✅ **No command line** - simple web interface
- ✅ **Automatic version listing** - all releases available in dropdown
- ✅ **Wireless Wi-Fi and BLE updates** - once installed, USB is retained mainly
  for recovery

*Initial USB flashing powered by [ESP Web Tools](https://esphome.github.io/esp-web-tools/)*

### Command Line (Fallback)

The `upload` command uses Bluetooth and therefore cannot install firmware on a
new or unresponsive controller. For a first-time USB installation when the web
flasher is unavailable, follow
[Initial USB Flashing](DEVELOPMENT.md#initial-usb-flashing) to build and upload
the matching V1 or V2 target with PlatformIO.

For an existing Smart Grind installation, enable Bluetooth on the grinder and
upload a matching release image with:

```bash
python3 tools/grinder.py upload smart-grind-by-weight-vX.X.X.bin
```

**Manual firmware download:** [Community releases page](https://github.com/Clinteastman/smart-grind-by-weight/releases)

**Build from source:** See [DEVELOPMENT.md](DEVELOPMENT.md)

---

## Initial calibration

After flashing firmware, calibrate the load cell for accurate measurements:

1. **Access calibration**: Menu → Calibrate (Tools section)
2. **Empty calibration**: Remove all weight from scale platform → Press OK
3. **Weight calibration**:
   - Place known weight on scale (e.g., coffee mug with water)
   - Use +/- buttons to adjust displayed value to match actual weight
   - Press OK to complete

**Tip**: A coffee mug with water makes ideal calibration weight - weigh it on kitchen scale first.

### Auto-Tune Motor Response

The auto-tune feature models your grinder's motor response behavior by measuring the physical lag between relay activation and grounds production. This accounts for hardware variations like voltage differences (110V vs 220V), relay types (solid-state vs mechanical), and burr inertia across different grinder models. The default 50ms value works well for most setups, but if you experience unreliable pulse corrections or want to minimize coffee waste through hardware-specific optimization, run auto-tune via **Menu → Tune Pulses** (Tools section). The 1-2 minute calibration process finds the minimum reliable pulse duration for your specific hardware and saves it automatically.

If Pulse Tune repeatedly cannot finish, open **Menu → Grind Settings → Motor
Response** and set the latency manually between 30 ms and 300 ms. The web
settings page exposes the same stored value. Changes use 5 ms steps and take
effect immediately; rerunning Pulse Tune later replaces the manual value with
the measured result. Start conservatively and change only one step at a time,
because a value that is too short can make finishing pulses unreliable.

### Diagnostics System

The system includes comprehensive load cell health monitoring accessible via **Menu → Diagnostics**. A warning icon (⚠) appears in the top-right corner when diagnostics are active - tap it to navigate directly to the diagnostics page.

**Diagnostic Types:**
1. **Load Cell Not Calibrated** - Appears until calibration is completed via Menu → Calibrate (Tools section)
2. **Sustained Noise** - Triggers after 60 seconds of excessive noise; clears after 120 seconds of acceptable levels
3. **Mechanical Instability** - Detects sudden weight drops during grinding (3+ events); auto-resets on next grind or via manual reset

**Displayed Values:**
- **Motor Latency** - Current motor response latency in milliseconds (default:
  50ms, measured by Pulse Tune or set manually in Grind Settings)
- **Calibration Factor** - Load cell calibration factor from Menu → Calibrate (Tools section)

**Noise Floor Diagnostics:**

Access via **Menu → Diagnostics → Noise Floor**.

**Three values displayed:**
1. **Standard Deviation (grams)** - Noise level in calibrated weight units
2. **Standard Deviation (ADC)** - Raw sensor noise values
3. **Noise Level Indicator** - Shows if noise will cause slow taring (>2s) or timeouts

**Important:** Noise diagnostics require prior calibration as they're based on calibrated gram values. High noise readings indicate wiring issues (check shield connection, use shorter wire leads). Read diagnostics in a stable, vibration-free environment for accurate assessment.

---

**Previous:** [Hardware and installation](HARDWARE_INSTALLATION.md) ·
**Next:** [Everyday use →](USER_GUIDE.md)
