#!/usr/bin/env python3
"""
PlatformIO post-build script to archive firmware binaries.
"""

Import("env")
import os
import json
import hashlib
from datetime import datetime
import shutil


def get_build_number(env):
    """Get the current BUILD_NUMBER from git_info.h"""
    import re
    try:
        # Get project directory from env parameter
        project_dir = env.get("PROJECT_DIR")
        git_info_path = os.path.join(project_dir, "src", "config", "git_info.h")
        with open(git_info_path, 'r') as f:
            content = f.read()
            match = re.search(r'#define BUILD_NUMBER (\d+)', content)
            if match:
                return int(match.group(1))
    except Exception as e:
        print(f"Warning: Could not read BUILD_NUMBER: {e}")
    return 0

def archive_firmware(source, target, env):
    """Archive firmware binary with build number naming"""
    print("=== Post-Build Firmware Archive ===")
    
    # Get project directory and build info
    project_dir = env.get("PROJECT_DIR")
    
    # Construct firmware path
    pioenv = env.get("PIOENV")
    build_dir = os.path.join(project_dir, ".pio", "build", pioenv)
    firmware_path = os.path.join(build_dir, "firmware.bin")
    
    if not os.path.exists(firmware_path):
        print(f"❌ Firmware binary not found: {firmware_path}")
        return
    
    # Keep incompatible board images in separate cache locations. Release
    # builds intentionally give V1 and V2 the same build number, so storing
    # both at firmware_cache/build_NNN.bin would silently overwrite V1.
    cache_dir = os.path.join(project_dir, "firmware_cache")
    if pioenv.endswith("-v2"):
        cache_dir = os.path.join(cache_dir, "waveshare-164-v2")
    
    # Get build number and firmware info
    build_number = get_build_number(env)
    firmware_size = os.path.getsize(firmware_path)
    
    print(f"📦 Firmware: {firmware_size:,} bytes ({firmware_size/1024:.1f} KB)")
    print(f"🔢 Build number: {build_number}")
    
    # Create cache directory if needed
    os.makedirs(cache_dir, exist_ok=True)
    
    # Archive the firmware with build number naming
    cached_firmware_path = os.path.join(cache_dir, f"build_{build_number:03d}.bin")
    
    try:
        shutil.copy2(firmware_path, cached_firmware_path)
        print(f"✅ Archived firmware: build_{build_number:03d}.bin")
        print(f"📁 Cache location: {cached_firmware_path}")
        
    except Exception as e:
        print(f"❌ Failed to archive firmware: {e}")
        return
    
    print("==========================================")

# This function is called by PlatformIO
env.AddPostAction("buildprog", archive_firmware)
env.AddPostAction("upload", archive_firmware)
