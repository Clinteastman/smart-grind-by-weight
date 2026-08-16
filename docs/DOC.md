# Smart Grind-by-Weight Documentation

Choose what you are trying to do. Each guide is short enough to use while you
are working; you no longer need to search one very long page.

> [!CAUTION]
> This modification involves a mains-powered appliance. Disconnect the grinder
> from mains before opening it, preserve protective earth and insulation, and
> ask a qualified person to handle mains wiring if you are not competent to do
> so. Bench-test the controller and scale from USB before connecting the motor.

## Start here

### I am building one for the first time

1. Confirm your grinder in the
   [compatibility matrix](GRINDER_COMPATIBILITY.md).
2. Use the [hardware and installation guide](HARDWARE_INSTALLATION.md) to buy
   parts, print mounts and understand the wiring.
3. Identify whether the Waveshare display needs V1 or V2 firmware in the
   [firmware and first-setup guide](FIRMWARE_SETUP.md).
4. Flash and test the display, touch controller and scale using USB power only.
5. Disconnect all power, install the components and recheck every connection.
6. Calibrate the scale, then perform a controlled manual test.
7. Use the [everyday guide](USER_GUIDE.md) to configure profiles and optional
   automation.

### I already have a working grinder

| I want to… | Go to |
| --- | --- |
| Update or recover the firmware | [Firmware, updates and calibration](FIRMWARE_SETUP.md) |
| Learn the touchscreen and web controls | [Everyday use](USER_GUIDE.md) |
| Diagnose a fault or download logs | [Diagnostics and data](DIAGNOSTICS_AND_DATA.md) |
| Understand predictive stopping and pulses | [How Smart Grind works](HOW_IT_WORKS.md) |
| Solve a known problem | [Troubleshooting](TROUBLESHOOTING.md) |
| Develop or build from source | [Development guide](DEVELOPMENT.md) |

## Guide map

### [Hardware and installation](HARDWARE_INSTALLATION.md)

Parts, printable components, assembly video, V1/V2 pin maps, recommended wire
lengths and physical installation.

### [Firmware and first setup](FIRMWARE_SETUP.md)

Display-generation identification, browser flashing, Wi-Fi/Bluetooth updates,
USB fallback, initial calibration and pulse auto-tuning.

### [Everyday use](USER_GUIDE.md)

Manual, Weight and Time modes; profiles; gestures; menu navigation;
screensavers; automatic cup actions; Bluetooth and the local web interface.

### [Diagnostics and data](DIAGNOSTICS_AND_DATA.md)

On-device and browser diagnostics, retained logs, grind history, downloads and
the legacy Python archive tools.

### [How Smart Grind works](HOW_IT_WORKS.md)

Predictive grinding, settling, correction pulses, motor-latency learning,
safety limits and frequently asked questions.

## Quick links

**[Install firmware](https://clinteastman.github.io/smart-grind-by-weight/)** ·
**[Latest release](https://github.com/Clinteastman/smart-grind-by-weight/releases/latest)** ·
**[Compatibility](GRINDER_COMPATIBILITY.md)** ·
**[3D designs](3D_PRINTS.md)** ·
**[Troubleshooting](TROUBLESHOOTING.md)** ·
**[Ask for help](https://github.com/Clinteastman/smart-grind-by-weight/issues)**

<details>
<summary>Links for old bookmarks into the previous all-in-one guide</summary>

The material has moved, but these headings remain so older links still lead to
the correct new guide.

## 🛠️ Parts List

See [Hardware and installation](HARDWARE_INSTALLATION.md#parts-list).

## 📹 Assembly Video

See [Hardware and installation](HARDWARE_INSTALLATION.md#assembly-video).

## 🔌 Installation & Wiring

See [Hardware and installation](HARDWARE_INSTALLATION.md#installation-and-wiring).

## 🚀 Firmware Installation

See [Firmware and first setup](FIRMWARE_SETUP.md#firmware-installation).

## ⚖️ Initial Calibration

See [Firmware and first setup](FIRMWARE_SETUP.md#initial-calibration).

## 📱 Usage Guide

See [Everyday use](USER_GUIDE.md#usage-guide).

## 🗺️ User Interface Navigation

See [Everyday use](USER_GUIDE.md#user-interface-navigation).

## ⚡ Automated Grind Flow

See [Everyday use](USER_GUIDE.md#automated-grind-flow).

## 🔵 Bluetooth Connectivity

See [Everyday use](USER_GUIDE.md#bluetooth-connectivity).

## 🔍 Diagnostic Report

See [Diagnostics and data](DIAGNOSTICS_AND_DATA.md#diagnostic-report).

## Screensaver

See [Everyday use](USER_GUIDE.md#screensaver).

## 📊 Analytics & Data Export

See [Diagnostics and data](DIAGNOSTICS_AND_DATA.md#analytics-and-data-export).

## 🧠 Algorithm Details

See [How Smart Grind works](HOW_IT_WORKS.md#algorithm-details).

## ❓ Frequently Asked Questions

See [How Smart Grind works](HOW_IT_WORKS.md#frequently-asked-questions).

## 🔧 Troubleshooting

See the dedicated [troubleshooting guide](TROUBLESHOOTING.md).

</details>
