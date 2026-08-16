# Diagnostics, Logs and Grind Data

Use this guide to inspect device health, collect support information and review
or export recorded grinds. For symptom-specific fixes, go directly to
[Troubleshooting](TROUBLESHOOTING.md).

**On this page:** [Diagnostic reports](#diagnostic-report) ·
[Report contents](#report-contents) · [Access methods](#access-methods) ·
[History and downloads](#analytics-and-data-export)

## Diagnostic report

Generate a comprehensive diagnostic report from your device for troubleshooting or attaching to GitHub issues. The report includes firmware version, system health metrics, load cell diagnostics, and all compile-time parameters.

For routine checks without opening the grinder, visit `http://smartgrind.local`,
open **Settings → System & updates**, and refresh or download the recent
diagnostic log. The latest 4 KiB of boot/runtime messages are retained in RAM,
so this adds no ongoing flash wear. USB serial is only needed when the firmware
cannot boot far enough to reconnect to Wi-Fi; the full compile-time diagnostic
report below remains available through the legacy Bluetooth tooling.

### Report Contents

- **Firmware Information**: Version, build number, git commit, branch, and build timestamp
- **System Runtime**: Uptime, CPU frequency, heap memory usage, flash size, and driver type
- **Runtime Diagnostics**:
  - Load cell calibration status and factor
  - Noise levels (standard deviation in grams and ADC units)
  - Noise acceptability assessment
  - Motor response latency (default or auto-tuned value)
- **Compile-Time Parameters**: Profile defaults, weight/time ranges, screen settings, auto-grind thresholds, and all user-configurable constants

### Access Methods

**Web Flasher (Recommended):**
1. Visit the [Community Web Flasher Tool](https://clinteastman.github.io/smart-grind-by-weight/)
2. Navigate to the **Diagnostics** tab
3. Click "Connect & Get Diagnostics"
4. Copy to clipboard or download the report as a text file

**Command Line:**
```bash
# Display report in terminal
python3 tools/grinder.py diagnostics

# Save report to file
python3 tools/grinder.py diagnostics --save diagnostic-report.txt
```

**When to Use:**
- Reporting bugs or issues on GitHub (attach the report to your issue)
- Verifying calibration status and noise levels
- Checking motor latency settings after auto-tune
- Confirming firmware version and compile-time parameters
- General troubleshooting and system health assessment

---

## Analytics and data export

Grind session logging is enabled by default. Open `http://smartgrind.local` and
choose **History** to inspect the latest 10 sessions, including accuracy,
consistency, weight and flow graphs. Download any session as CSV, JSON or its
original raw record directly in the browser.

The Python dashboard remains a legacy archive/recovery option when you want to
collect sessions beyond the device's bounded history:

### Launch Interactive Dashboard
```bash
# Export data and launch Streamlit dashboard
python3 tools/grinder.py analyze

# Or view reports from existing data
python3 tools/grinder.py report
```

### Available Tools
```bash
python3 tools/grinder.py --help          # Show all available commands
python3 tools/grinder.py scan            # Scan for BLE devices
python3 tools/grinder.py connect         # Connect to grinder device
python3 tools/grinder.py debug           # Stream live debug logs
python3 tools/grinder.py info            # Get device system information
python3 tools/grinder.py export          # Export grind data to database
```

### Tools Directory Structure
- **`grinder.py`**: Cross-platform Python tool for all operations (build, upload, analyze)
- **`ble/`**: BLE communication tools and OTA update system
- **`streamlit-reports/`**: Interactive data visualization and analytics
- **`database/`**: SQLite database management for grind session storage

---

**Previous:** [Everyday use](USER_GUIDE.md) ·
**Next:** [How Smart Grind works →](HOW_IT_WORKS.md)
