#!/usr/bin/env python3
"""
Unified Grinder Tool - Cross-platform Python replacement for bash script
Single script for all grinder operations: build, upload, export, analyze, report
"""

import argparse
import asyncio
import os
import sys
import subprocess
import platform
import venv
from pathlib import Path
from typing import Optional, List, Dict, Any, Tuple
import shutil
import stat
import json
import binascii
import struct
import tempfile

# Color support for cross-platform output
try:
    from colorama import init, Fore, Style
    init(autoreset=True)
    COLORS = {
        'RED': Fore.RED,
        'GREEN': Fore.GREEN,
        'YELLOW': Fore.YELLOW,
        'BLUE': Fore.BLUE,
        'PURPLE': Fore.MAGENTA,
        'CYAN': Fore.CYAN,
        'RESET': Style.RESET_ALL
    }
except ImportError:
    # Fallback for systems without colorama
    COLORS = {k: '' for k in ['RED', 'GREEN', 'YELLOW', 'BLUE', 'PURPLE', 'CYAN', 'RESET']}

# Firmware variants. Each builds in build/<name>/ with its own sdkconfig, made
# from sdkconfig.defaults plus the variant's overlay. V1 and V2 images are
# archived under firmware_cache/<archive>/ as Bluetooth delta bases and for
# flash-usb.
FIRMWARE_VARIANTS = {
    "v1": {"overlay": None, "archive": "waveshare-164-v1"},
    "v2": {"overlay": "sdkconfig.defaults.v2", "archive": "waveshare-164-v2"},
    "debug": {"overlay": "sdkconfig.defaults.debug", "archive": None},
    "mock": {"overlay": "sdkconfig.defaults.mock", "archive": None},
}
FIRMWARE_IMAGE = "smart-grind-by-weight.bin"
IDF_ACTIVATION_HINT = (
    "Activate ESP-IDF v6.1 in this shell first, for example "
    "'source ~/.espressif/tools/activate_idf_v6.1.sh' (EIM) or "
    "'. ~/esp/esp-idf/export.sh'"
)

class GrinderTool:
    """Unified grinder tool for cross-platform operations."""
    
    def __init__(self):
        self.script_dir = Path(__file__).parent
        self.project_dir = self.script_dir.parent
        self.venv_dir = self.script_dir / "venv"
        
        # Platform-specific paths
        if platform.system() == "Windows":
            self.venv_python = self.venv_dir / "Scripts" / "python.exe"
            self.venv_pip = self.venv_dir / "Scripts" / "pip.exe"
            self.venv_streamlit = self.venv_dir / "Scripts" / "streamlit.exe"
        else:
            self.venv_python = self.venv_dir / "bin" / "python3"
            self.venv_pip = self.venv_dir / "bin" / "pip"
            self.venv_streamlit = self.venv_dir / "bin" / "streamlit"
        
        self.ble_tool = self.script_dir / "ble" / "grinder-ble.py"
        self.streamlit_dir = self.script_dir / "streamlit-reports"
        self.db_path = self.script_dir / "database" / "grinder_data.db"
        self.requirements_txt = self.script_dir / "requirements.txt"
        self.build_root = self.project_dir / "build"
        self.build_lock_path = self.build_root / ".smart-grind-build.lock"
    
    def safe_print(self, text: str):
        """Print text with proper encoding handling for all platforms."""
        try:
            print(text)
        except UnicodeEncodeError:
            # Replace problematic Unicode chars for Windows
            safe_text = text.encode('ascii', 'replace').decode('ascii')
            print(safe_text)
    
    def print_header(self, message: str):
        """Print a formatted header."""
        self.safe_print(f"{COLORS['BLUE']}=== {message} ==={COLORS['RESET']}")
    
    def print_success(self, message: str):
        """Print a success message."""
        self.safe_print(f"{COLORS['GREEN']}[OK] {message}{COLORS['RESET']}")
    
    def print_error(self, message: str):
        """Print an error message."""
        self.safe_print(f"{COLORS['RED']}[ERROR] {message}{COLORS['RESET']}")
    
    def print_warning(self, message: str):
        """Print a warning message."""
        self.safe_print(f"{COLORS['YELLOW']}[WARNING] {message}{COLORS['RESET']}")
    
    def print_info(self, message: str):
        """Print an info message."""
        self.safe_print(f"{COLORS['CYAN']}[INFO] {message}{COLORS['RESET']}")

    def _process_is_running(self, pid: int) -> bool:
        if pid <= 0:
            return False
        if platform.system() == "Windows":
            # os.kill(pid, 0) is not a harmless existence probe on Windows:
            # CPython can route it through TerminateProcess. Query the handle
            # instead so checking a lock can never stop the active build.
            try:
                import ctypes
                kernel32 = ctypes.windll.kernel32
                handle = kernel32.OpenProcess(0x1000, False, pid)  # QUERY_LIMITED_INFORMATION
                if not handle:
                    return False
                try:
                    exit_code = ctypes.c_ulong()
                    return bool(kernel32.GetExitCodeProcess(handle, ctypes.byref(exit_code))) \
                        and exit_code.value == 259  # STILL_ACTIVE
                finally:
                    kernel32.CloseHandle(handle)
            except (AttributeError, OSError):
                return False
        try:
            os.kill(pid, 0)
            return True
        except OSError:
            return False

    def _windows_project_compilers(self) -> List[int]:
        """Find orphaned compiler children still writing this checkout."""
        if platform.system() != "Windows":
            return []
        build_path = str(self.build_root).replace("'", "''")
        script = (
            "$p='" + build_path + "'; "
            "Get-CimInstance Win32_Process | Where-Object { "
            "$_.Name -match 'ninja|cmake|python|cmd|xtensa|cc1plus|ld' -and "
            "$_.CommandLine -and $_.CommandLine.Contains($p) } | "
            "Select-Object -ExpandProperty ProcessId | ConvertTo-Json -Compress"
        )
        try:
            result = subprocess.run(
                ["powershell", "-NoProfile", "-Command", script],
                capture_output=True, text=True, check=False, timeout=10,
            )
            if result.returncode != 0 or not result.stdout.strip():
                return []
            value = json.loads(result.stdout)
            return [int(value)] if isinstance(value, int) else [int(pid) for pid in value]
        except (OSError, ValueError, subprocess.TimeoutExpired):
            return []

    def _acquire_build_lock(self, environment: str) -> bool:
        """Atomically prevent overlapping builds and detect orphaned compilers."""
        self.build_lock_path.parent.mkdir(parents=True, exist_ok=True)
        for _ in range(2):
            try:
                fd = os.open(self.build_lock_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
                with os.fdopen(fd, "w", encoding="utf-8") as lock:
                    json.dump({"pid": os.getpid(), "environment": environment}, lock)
                return True
            except FileExistsError:
                try:
                    lock_data = json.loads(self.build_lock_path.read_text(encoding="utf-8"))
                    owner_pid = int(lock_data.get("pid", 0))
                except (OSError, ValueError, json.JSONDecodeError):
                    owner_pid = 0
                if self._process_is_running(owner_pid):
                    self.print_error(f"A Smart Grind build is already running (process {owner_pid})")
                    return False
                compiler_pids = self._windows_project_compilers()
                if compiler_pids:
                    self.print_error(
                        "An earlier Smart Grind build left compiler processes running: " +
                        ", ".join(str(pid) for pid in compiler_pids)
                    )
                    self.print_info("Wait for them to finish or stop those processes before rebuilding")
                    return False
                self.build_lock_path.unlink(missing_ok=True)
        self.print_error("Could not acquire the Smart Grind build lock")
        return False

    def _release_build_lock(self):
        try:
            lock_data = json.loads(self.build_lock_path.read_text(encoding="utf-8"))
            if int(lock_data.get("pid", 0)) == os.getpid():
                self.build_lock_path.unlink(missing_ok=True)
        except (OSError, ValueError, json.JSONDecodeError):
            pass

    def _idf_python(self) -> Optional[Path]:
        """The Python interpreter of the ESP-IDF environment active in this shell."""
        python_env = os.environ.get("IDF_PYTHON_ENV_PATH")
        if python_env:
            if platform.system() == "Windows":
                return Path(python_env) / "Scripts" / "python.exe"
            return Path(python_env) / "bin" / "python"
        if os.environ.get("IDF_PATH"):
            # Activation puts the ESP-IDF Python environment first on PATH.
            found = shutil.which("python") or shutil.which("python3")
            return Path(found) if found else None
        return None

    def _idf_command(self, variant: str, *actions: str) -> Optional[List[str]]:
        """idf.py invocation for one firmware variant, or None without ESP-IDF."""
        idf_path = os.environ.get("IDF_PATH")
        python = self._idf_python()
        if not idf_path or python is None:
            return None
        build_dir = self.build_root / variant
        defaults = ["sdkconfig.defaults"]
        if FIRMWARE_VARIANTS[variant]["overlay"]:
            defaults.append(FIRMWARE_VARIANTS[variant]["overlay"])
        return [
            str(python), str(Path(idf_path) / "tools" / "idf.py"),
            "-C", str(self.project_dir), "-B", str(build_dir),
            "-D", f"SDKCONFIG={build_dir / 'sdkconfig'}",
            "-D", "SDKCONFIG_DEFAULTS=" + ";".join(defaults),
            *actions,
        ]

    def _esptool(self) -> Optional[List[str]]:
        """esptool from the ESP-IDF environment, or from the project venv."""
        for python in (self._idf_python(), self.venv_python):
            if python is not None and python.exists():
                probe = subprocess.run(
                    [str(python), "-m", "esptool", "version"],
                    capture_output=True, text=True, check=False,
                )
                if probe.returncode == 0:
                    return [str(python), "-m", "esptool"]
        return None

    def _build_number(self) -> Optional[int]:
        """BUILD_NUMBER of the most recent build, from src/config/git_info.h."""
        import re
        try:
            header = (self.project_dir / "src" / "config" / "git_info.h").read_text()
        except OSError:
            return None
        match = re.search(r"#define BUILD_NUMBER (\d+)", header)
        return int(match.group(1)) if match else None

    def _archive_firmware(self, variant: str):
        """Keep a V1 or V2 image under its build number, as a delta base for
        Bluetooth updates and for flash-usb."""
        archive = FIRMWARE_VARIANTS[variant]["archive"]
        image = self.build_root / variant / FIRMWARE_IMAGE
        if not archive or not image.exists():
            return
        build_number = self._build_number()
        if build_number is None:
            self.print_warning("Could not read the build number; firmware was not archived")
            return
        archive_dir = self.project_dir / "firmware_cache" / archive
        archive_dir.mkdir(parents=True, exist_ok=True)
        target = archive_dir / f"build_{build_number:03d}.bin"
        shutil.copy2(image, target)
        self.print_info(
            f"Build #{build_number}: {image.stat().st_size:,} bytes, "
            f"archived as firmware_cache/{archive}/{target.name}"
        )
    
    def check_venv(self) -> bool:
        """Check if virtual environment exists and is properly set up."""
        if not self.venv_python.exists():
            self.print_warning("Virtual environment not found, setting up automatically...")
            return self.install_dependencies()
        return True
    
    def install_dependencies(self) -> bool:
        """Create virtual environment and install dependencies."""
        try:
            if not self.venv_dir.exists():
                self.print_info("Creating virtual environment...")
                
                # Find Python executable
                python_cmd = None
                for cmd in ["python3", "python"]:
                    if shutil.which(cmd):
                        python_cmd = cmd
                        break
                
                if not python_cmd:
                    self.print_error("Python not found. Please install Python 3.8+")
                    return False
                
                # Create virtual environment
                venv.create(self.venv_dir, with_pip=True)
            
            self.print_info("Installing Python packages...")
            if not self.venv_pip.exists():
                self.print_error(f"Virtual environment pip not found at: {self.venv_pip}")
                self.print_info(f"Platform: {platform.system()}, Expected venv structure may be incorrect")
                return False
            
            # Install requirements
            result = subprocess.run([
                str(self.venv_pip), "install", "-q", "-r", str(self.requirements_txt)
            ], capture_output=True, text=True)
            
            if result.returncode != 0:
                self.print_error(f"Failed to install dependencies: {result.stderr}")
                return False
            
            # Also install colorama if not already present
            subprocess.run([
                str(self.venv_pip), "install", "-q", "colorama"
            ], capture_output=True, text=True)
            
            return True
            
        except Exception as e:
            self.print_error(f"Failed to set up environment: {e}")
            return False
    
    def run_command(
        self,
        cmd: List[str],
        cwd: Optional[Path] = None,
        capture_output: bool = False,
        env: Optional[Dict[str, str]] = None,
    ) -> subprocess.CompletedProcess:
        """Run a command with proper error handling."""
        try:
            return subprocess.run(
                cmd, 
                cwd=cwd or self.project_dir,
                capture_output=capture_output,
                text=True,
                check=False,
                env=env,
            )
        except FileNotFoundError as e:
            self.print_error(f"Command not found: {cmd[0]}")
            raise e
    
    async def run_async_command(self, cmd: List[str], cwd: Optional[Path] = None) -> int:
        """Run an async command (for BLE operations)."""
        try:
            process = await asyncio.create_subprocess_exec(
                *cmd,
                cwd=cwd or self.project_dir,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT
            )
            
            # Stream output in real-time
            while True:
                line = await process.stdout.readline()
                if not line:
                    break
                print(line.decode().rstrip())
            
            await process.wait()
            return process.returncode
            
        except Exception as e:
            self.print_error(f"Command failed: {e}")
            return 1
    
    def cmd_build(self, args: argparse.Namespace) -> int:
        """Build one firmware variant with ESP-IDF's idf.py."""
        self.print_header("Building Firmware")
        variant = getattr(args, "hardware", "v1")
        command = self._idf_command(variant, "build")
        if command is None:
            self.print_error("ESP-IDF is not set up in this shell")
            self.print_info(IDF_ACTIVATION_HINT)
            return 1
        if not self._acquire_build_lock(variant):
            return 2
        jobs = max(1, getattr(args, "jobs", min(8, os.cpu_count() or 1)))
        self.print_info(f"Target: {variant.upper()} in build/{variant} ({jobs} parallel jobs)")

        build_env = os.environ.copy()
        # ESP-IDF's tools print Unicode status symbols. Force UTF-8 so
        # redirected Windows builds do not lose their final output to a
        # background cp1252 UnicodeEncodeError.
        build_env.setdefault("PYTHONUTF8", "1")
        build_env.setdefault("PYTHONIOENCODING", "utf-8")
        # Keep local Windows builds responsive: ninja otherwise uses every
        # logical CPU, which slows this source-heavy project through compiler
        # and filesystem contention.
        build_env["IDF_PY_BUILD_JOBS"] = str(jobs)
        try:
            result = self.run_command(command, env=build_env)
        finally:
            self._release_build_lock()

        if result.returncode != 0:
            self.print_error("Build failed")
            return result.returncode
        self._archive_firmware(variant)
        self.print_success("Firmware build completed")
        return 0
    
    async def cmd_upload(self, args: argparse.Namespace) -> int:
        """Upload firmware via BLE OTA."""
        firmware_path = args.firmware
        
        if not firmware_path:
            self.print_info("Finding latest firmware file...")
            firmware_files = [
                self.build_root / variant / FIRMWARE_IMAGE
                for variant in FIRMWARE_VARIANTS
                if (self.build_root / variant / FIRMWARE_IMAGE).exists()
            ]

            if not firmware_files:
                self.print_error("No firmware file found")
                self.print_info("Run: python3 grinder.py build")
                return 1
            
            # Get the most recently modified firmware
            firmware_path = max(firmware_files, key=lambda f: f.stat().st_mtime)
        
        self.print_header("BLE OTA Upload")
        self.print_info(f"Using firmware: {firmware_path}")
        
        if not self.check_venv():
            return 1
        
        cmd = [str(self.venv_python), str(self.ble_tool), "upload", str(firmware_path)]
        
        # Add additional arguments
        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])
        if hasattr(args, 'force_full') and args.force_full:
            cmd.append("--force-full")
        
        return await self.run_async_command(cmd)
    
    async def cmd_build_upload(self, args: argparse.Namespace) -> int:
        """Build firmware and upload via BLE."""
        build_result = self.cmd_build(args)
        if build_result != 0:
            return build_result
        
        # Use the firmware just built for the requested hardware. Selecting the
        # newest file across all variants can accidentally upload V1 to V2
        # (or vice versa) after a multi-target validation run.
        args.firmware = str(self.build_root / args.hardware / FIRMWARE_IMAGE)
        return await self.cmd_upload(args)
    
    async def cmd_export(self, args: argparse.Namespace) -> int:
        """Export grind data from device."""
        self.print_header("Exporting Grind Data")
        
        if not self.check_venv():
            return 1
        
        cmd = [str(self.venv_python), str(self.ble_tool), "export"]
        
        if hasattr(args, 'db') and args.db:
            cmd.extend(["--db", args.db])
        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])
        
        return await self.run_async_command(cmd)
    
    async def cmd_analyze(self, args: argparse.Namespace) -> int:
        """Export data and launch Streamlit report."""
        self.print_header("Data Analysis Workflow")
        
        if not self.check_venv():
            return 1
        
        cmd = [str(self.venv_python), str(self.ble_tool), "analyse"]
        
        if hasattr(args, 'db') and args.db:
            cmd.extend(["--db", args.db])
        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])
        
        return await self.run_async_command(cmd)
    
    def cmd_report(self, args: argparse.Namespace) -> int:
        """Launch Streamlit report from existing data."""
        self.print_header("Launching Streamlit Report")
        
        # Determine database file
        db_file = self.db_path
        if hasattr(args, 'db') and args.db:
            if Path(args.db).is_absolute():
                db_file = Path(args.db)
            else:
                db_file = self.script_dir / "database" / args.db
        
        if not db_file.exists():
            self.print_error(f"Database file not found: {db_file}")
            self.print_info("Run: python3 grinder.py export")
            return 1
        
        if not self.check_venv():
            return 1
        
        # Check if streamlit exists in venv
        if not self.venv_streamlit.exists():
            self.print_warning("Streamlit not found, installing...")
            result = subprocess.run([
                str(self.venv_pip), "install", "streamlit>=1.28.0", "plotly>=5.15.0"
            ], capture_output=True, text=True)
            
            if result.returncode != 0:
                self.print_error(f"Failed to install Streamlit: {result.stderr}")
                return 1
        
        self.print_info(f"Using database: {db_file}")
        self.print_info("Opening at: http://localhost:8501")
        self.print_info("Press Ctrl+C to stop the server")
        
        # Set environment variables and launch streamlit
        env = os.environ.copy()
        env["GRIND_DB_PATH"] = str(db_file)
        env["PYTHONPATH"] = str(self.streamlit_dir)
        
        try:
            result = subprocess.run([
                str(self.venv_python), "-m", "streamlit", "run", "grind_report.py"
            ], cwd=self.streamlit_dir, env=env)
            return result.returncode
        except KeyboardInterrupt:
            self.print_info("Streamlit server stopped")
            return 0
    
    async def cmd_scan(self, args: argparse.Namespace) -> int:
        """Scan for BLE devices."""
        self.print_header("Scanning for BLE Devices")
        
        if not self.check_venv():
            return 1
        
        cmd = [str(self.venv_python), str(self.ble_tool), "scan"]
        return await self.run_async_command(cmd)
    
    async def cmd_connect(self, args: argparse.Namespace) -> int:
        """Connect to grinder device."""
        self.print_header("Connecting to Grinder")
        
        if not self.check_venv():
            return 1
        
        cmd = [str(self.venv_python), str(self.ble_tool), "connect"]
        
        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])
        
        return await self.run_async_command(cmd)
    
    async def cmd_debug(self, args: argparse.Namespace) -> int:
        """Stream live debug logs from device."""
        self.print_header("Debug Monitor")
        
        if not self.check_venv():
            return 1
        
        cmd = [str(self.venv_python), str(self.ble_tool), "debug"]
        
        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])
        
        return await self.run_async_command(cmd)
    
    async def cmd_info(self, args: argparse.Namespace) -> int:
        """Get device system information."""
        self.print_header("Device System Information")

        if not self.check_venv():
            return 1

        cmd = [str(self.venv_python), str(self.ble_tool), "info"]

        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])

        return await self.run_async_command(cmd)

    def cmd_flash_usb(self, args: argparse.Namespace) -> int:
        """Flash a previously successful firmware image without rebuilding it."""
        self.print_header("Flashing Firmware over USB")
        firmware_path = Path(args.firmware).resolve() if args.firmware else None
        if firmware_path is None:
            cache_dir = self.project_dir / "firmware_cache" / FIRMWARE_VARIANTS[args.hardware]["archive"]
            candidates = sorted(cache_dir.glob("build_*.bin"), key=lambda path: path.stat().st_mtime)
            if not candidates and args.hardware == "v1":
                # Compatibility with archives made before hardware-specific directories.
                candidates = sorted(
                    (self.project_dir / "firmware_cache").glob("build_*.bin"),
                    key=lambda path: path.stat().st_mtime,
                )
            if not candidates:
                self.print_error(f"No archived {args.hardware.upper()} firmware was found")
                self.print_info(f"Build it first: tools/grinder.py build --hardware {args.hardware}")
                return 1
            firmware_path = candidates[-1]

        if not firmware_path.is_file():
            self.print_error(f"Firmware file not found: {firmware_path}")
            return 1
        with firmware_path.open("rb") as firmware:
            if firmware.read(1) != b"\xe9":
                self.print_error("Firmware is not an ESP32 application image")
                return 1
        if firmware_path.stat().st_size > 3 * 1024 * 1024:
            self.print_error("Firmware is larger than the 3 MB application partition")
            return 1
        environment = f"usb-{args.hardware}-{args.port}"
        if not self._acquire_build_lock(environment):
            return 2
        try:
            esptool = self._esptool()
            if not esptool:
                self.print_error("Could not find an esptool installation")
                self.print_info(IDF_ACTIVATION_HINT + ", or run 'python3 tools/grinder.py install'")
                return 1
            flash_env = os.environ.copy()
            flash_env["PYTHONUTF8"] = "1"
            flash_env["PYTHONIOENCODING"] = "utf-8"
            ota_file = tempfile.NamedTemporaryFile(suffix="-otadata.bin", delete=False)
            ota_path = Path(ota_file.name)
            ota_file.close()
            try:
                read_result = self.run_command(
                    esptool + ["--chip", "esp32s3",
                     "--port", args.port, "--baud", str(args.baud),
                     "--before", "default-reset", "--after", "hard-reset",
                     "read-flash", "0x0000e000", "0x2000", str(ota_path)],
                    env=flash_env,
                )
                if read_result.returncode != 0:
                    self.print_error("Could not read the board's active firmware slot")
                    return read_result.returncode
                app_offset, app_label = self._active_app_partition(ota_path.read_bytes())
            finally:
                ota_path.unlink(missing_ok=True)

            self.print_info(f"Image: {firmware_path.name} ({firmware_path.stat().st_size:,} bytes)")
            self.print_info(
                f"Port: {args.port}; writing active {app_label} at 0x{app_offset:08x} "
                "(NVS and LittleFS are preserved)"
            )
            result = self.run_command(
                esptool + ["--chip", "esp32s3",
                 "--port", args.port, "--baud", str(args.baud),
                 "--before", "default-reset", "--after", "hard-reset",
                 "write-flash", f"0x{app_offset:08x}", str(firmware_path)],
                env=flash_env,
            )
            if result.returncode == 0:
                self.print_success("USB firmware flash completed and verified")
            else:
                self.print_error("USB firmware flash failed")
            return result.returncode
        finally:
            self._release_build_lock()

    @staticmethod
    def _active_app_partition(otadata: bytes) -> Tuple[int, str]:
        """Return the boot-selected app offset without modifying OTA metadata."""
        if len(otadata) != 0x2000:
            raise ValueError("OTA data must be exactly 8 KB")

        entries = []
        for sector_offset in (0, 0x1000):
            ota_seq, ota_state, stored_crc = struct.unpack_from("<I20xII", otadata, sector_offset)
            expected_crc = binascii.crc32(struct.pack("<I", ota_seq), 0xFFFFFFFF) & 0xFFFFFFFF
            is_valid = (
                ota_seq != 0xFFFFFFFF
                and ota_state not in (3, 4)  # ESP_OTA_IMG_INVALID / ABORTED
                and stored_crc == expected_crc
            )
            if is_valid:
                entries.append(ota_seq)

        if not entries:
            # Erased/invalid otadata makes the ESP-IDF bootloader select factory.
            return 0x00020000, "factory"

        slot = (max(entries) - 1) % 2
        if slot == 0:
            return 0x00320000, "ota_0"
        return 0x00620000, "ota_1"

    async def cmd_preflight(self, args: argparse.Namespace) -> int:
        """Verify the BLE OTA path without writing firmware."""
        self.print_header("BLE OTA Preflight")
        if not self.check_venv():
            return 1
        cmd = [str(self.venv_python), str(self.ble_tool), "preflight"]
        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])
        return await self.run_async_command(cmd)

    async def cmd_diagnostics(self, args: argparse.Namespace) -> int:
        """Get comprehensive diagnostic report."""
        self.print_header("Diagnostic Report")

        if not self.check_venv():
            return 1

        cmd = [str(self.venv_python), str(self.ble_tool), "diagnostics"]

        if hasattr(args, 'device') and args.device:
            cmd.extend(["--device", args.device])

        if hasattr(args, 'save') and args.save:
            cmd.extend(["--save", args.save])

        return await self.run_async_command(cmd)

    def cmd_install(self, args: argparse.Namespace) -> int:
        """Manually install Python dependencies."""
        self.print_header("Installing Dependencies")
        
        if self.install_dependencies():
            self.print_success("Dependencies installed")
            return 0
        else:
            return 1
    
    def cmd_clean(self, args: argparse.Namespace) -> int:
        """Remove every variant's build directory, including its sdkconfig."""
        self.print_header("Cleaning Build Artifacts")
        if not self._acquire_build_lock("clean"):
            return 2
        try:
            for variant in FIRMWARE_VARIANTS:
                build_dir = self.build_root / variant
                if build_dir.exists():
                    shutil.rmtree(build_dir)
                    self.print_info(f"Removed build/{variant}")
        finally:
            self._release_build_lock()
        self.print_success("Build artifacts cleaned")
        return 0
    
    def cmd_release(self, args: argparse.Namespace) -> int:
        """Create a tagged release using the release helper script."""
        self.print_header("Creating Tagged Release")
        
        release_script = self.script_dir / "release.py"
        
        # Make sure the release script exists
        if not release_script.exists():
            self.print_error("Release script not found!")
            self.print_info("The release.py script should be in the tools/ directory")
            return 1
        
        # Run the release script
        try:
            result = subprocess.run([sys.executable, str(release_script)])
            return result.returncode
        except Exception as e:
            self.print_error(f"Failed to run release script: {e}")
            return 1

def create_parser() -> argparse.ArgumentParser:
    """Create the argument parser with all subcommands."""
    parser = argparse.ArgumentParser(
        description="Unified Grinder Tool - All-in-one grinder operations",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""
{COLORS['YELLOW']}Examples:{COLORS['RESET']}
  python3 grinder.py build-upload              # Build and upload firmware
  python3 grinder.py build-upload --force-full # Build and force full firmware update
  python3 grinder.py analyze                   # Export data and show interactive report
  python3 grinder.py report                    # Just show report from existing data
  python3 grinder.py export --db session1.db  # Export to custom database
  python3 grinder.py upload --device MyGrinder # Upload to specific device
  python3 grinder.py connect                   # Connect to grinder device
  python3 grinder.py info                      # Get device system information
        """
    )
    
    subparsers = parser.add_subparsers(dest='command', required=True, help='Available commands')
    
    # Build & Upload Commands
    build_parser = subparsers.add_parser('build', help='Build firmware with ESP-IDF (idf.py)')
    build_parser.add_argument('--hardware', choices=list(FIRMWARE_VARIANTS), default='v1',
                              help='Firmware variant to build: v1, v2, or the V1 debug and mock builds')
    build_parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 1), help='Parallel compiler jobs (default: at most 8)')
    
    upload_parser = subparsers.add_parser('upload', help='Upload firmware via BLE OTA')
    upload_parser.add_argument('firmware', nargs='?', help='Path to firmware .bin file (finds latest if not specified)')
    upload_parser.add_argument('--force-full', action='store_true', help='Force full firmware update (skip delta patching)')
    upload_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')

    usb_parser = subparsers.add_parser(
        'flash-usb', help='Flash an archived firmware image over USB without rebuilding')
    usb_parser.add_argument('firmware', nargs='?', help='Archived firmware .bin (latest matching hardware by default)')
    usb_parser.add_argument('--hardware', choices=['v1', 'v2'], default='v1', help='Hardware generation to flash')
    usb_parser.add_argument('--port', required=True, help='USB serial port, for example COM15')
    usb_parser.add_argument('--baud', type=int, default=921600, help='USB flashing baud rate')
    
    build_upload_parser = subparsers.add_parser('build-upload', help='Build firmware and upload via BLE')
    build_upload_parser.add_argument('--hardware', choices=['v1', 'v2'], default='v1', help='Hardware generation to build and upload')
    build_upload_parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 1), help='Parallel compiler jobs (default: at most 8)')
    build_upload_parser.add_argument('--force-full', action='store_true', help='Force full firmware update (skip delta patching)')
    build_upload_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    
    # Data & Analysis Commands
    export_parser = subparsers.add_parser('export', help='Export grind data from device to database')
    export_parser.add_argument('--db', help='Specify database file (default: grinder_data.db)')
    export_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    
    analyze_parser = subparsers.add_parser('analyze', help='Export data and launch Streamlit report')
    analyze_parser.add_argument('--db', help='Specify database file (default: grinder_data.db)')
    analyze_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    
    report_parser = subparsers.add_parser('report', help='Launch Streamlit report (no data export)')
    report_parser.add_argument('--db', help='Specify database file (default: grinder_data.db)')
    
    analyze_offline_parser = subparsers.add_parser('analyze-offline', help='Alias for report - uses existing database')
    analyze_offline_parser.add_argument('--db', help='Specify database file (default: grinder_data.db)')
    
    # BLE Commands
    scan_parser = subparsers.add_parser('scan', help='Scan for BLE devices')
    
    connect_parser = subparsers.add_parser('connect', help='Connect to grinder device')
    connect_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    
    debug_parser = subparsers.add_parser('debug', help='Stream live debug logs from device')
    debug_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    
    info_parser = subparsers.add_parser('info', help='Get comprehensive device system information')
    info_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')

    preflight_parser = subparsers.add_parser('preflight', help='Verify BLE OTA readiness without uploading')
    preflight_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')

    diagnostics_parser = subparsers.add_parser('diagnostics', help='Get comprehensive diagnostic report for GitHub issues')
    diagnostics_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    diagnostics_parser.add_argument('--save', metavar='FILE', help='Save report to file (default: print to console)')

    # Development Commands
    install_parser = subparsers.add_parser('install', help='Manually install Python dependencies (auto-setup when needed)')
    monitor_parser = subparsers.add_parser('monitor', help='Monitor live debug output via BLE (alias for debug)')
    monitor_parser.add_argument('--device', default='GrindByWeight', help='Specify device name')
    clean_parser = subparsers.add_parser('clean', help='Remove the build directories of every variant')
    release_parser = subparsers.add_parser('release', help='Create tagged release (triggers automated GitHub release)')
    
    return parser

async def main():
    """Main entry point."""
    parser = create_parser()
    args = parser.parse_args()
    
    tool = GrinderTool()
    
    try:
        # Map commands to methods
        if args.command == 'build':
            return tool.cmd_build(args)
        elif args.command == 'upload':
            return await tool.cmd_upload(args)
        elif args.command == 'flash-usb':
            return tool.cmd_flash_usb(args)
        elif args.command == 'build-upload':
            return await tool.cmd_build_upload(args)
        elif args.command == 'export':
            return await tool.cmd_export(args)
        elif args.command in ['analyze', 'analyse']:
            return await tool.cmd_analyze(args)
        elif args.command == 'report':
            return tool.cmd_report(args)
        elif args.command in ['analyze-offline', 'analyse-offline']:
            return tool.cmd_report(args)  # Same as report
        elif args.command == 'scan':
            return await tool.cmd_scan(args)
        elif args.command == 'connect':
            return await tool.cmd_connect(args)
        elif args.command in ['debug', 'monitor']:
            return await tool.cmd_debug(args)
        elif args.command == 'info':
            return await tool.cmd_info(args)
        elif args.command == 'preflight':
            return await tool.cmd_preflight(args)
        elif args.command == 'diagnostics':
            return await tool.cmd_diagnostics(args)
        elif args.command == 'install':
            return tool.cmd_install(args)
        elif args.command == 'clean':
            return tool.cmd_clean(args)
        elif args.command == 'release':
            return tool.cmd_release(args)
        else:
            tool.print_error(f"Unknown command: {args.command}")
            parser.print_help()
            return 1
            
    except KeyboardInterrupt:
        tool.print_info("Interrupted by user")
        return 1
    except Exception as e:
        tool.print_error(f"Unexpected error: {e}")
        return 1

if __name__ == "__main__":
    if platform.system() == "Windows":
        # On Windows, set the event loop policy to avoid issues
        asyncio.set_event_loop_policy(asyncio.WindowsProactorEventLoopPolicy())
    
    try:
        exit_code = asyncio.run(main())
        sys.exit(exit_code)
    except Exception as e:
        print(f"{COLORS['RED']}An unexpected error occurred: {e}{COLORS['RESET']}")
        sys.exit(1)
