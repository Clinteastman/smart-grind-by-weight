#!/usr/bin/env python3
"""
Pre-build script for PlatformIO build system.
This script automatically increments build numbers, captures Git commit ID and branch information,
and creates a header file with the build information.
"""

try:
    Import("env")
    platformio_mode = True
except:
    platformio_mode = False

import subprocess
import sys
import os

def get_git_info():
    """Get Git commit ID and branch information."""
    try:
        # Determine project root directory
        if platformio_mode:
            project_dir = env.get("PROJECT_DIR", os.getcwd())
        else:
            project_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
            
        # Get current Git commit hash (short form, 7 characters)
        commit_result = subprocess.run(
            ['git', 'rev-parse', '--short=7', 'HEAD'],
            capture_output=True,
            text=True,
            cwd=project_dir
        )
        
        if commit_result.returncode == 0:
            commit_id = commit_result.stdout.strip()
        else:
            commit_id = "unknown"
        
        # Get current Git branch
        branch_result = subprocess.run(
            ['git', 'rev-parse', '--abbrev-ref', 'HEAD'],
            capture_output=True,
            text=True,
            cwd=project_dir
        )
        
        if branch_result.returncode == 0:
            branch = branch_result.stdout.strip()
        else:
            branch = "unknown"
            
        # Check if there are uncommitted changes
        status_result = subprocess.run(
            ['git', 'status', '--porcelain'],
            capture_output=True,
            text=True,
            cwd=project_dir
        )
        
        if status_result.returncode == 0 and status_result.stdout.strip():
            # There are uncommitted changes, append + to commit ID
            commit_id += "+"
            
    except (subprocess.SubprocessError, FileNotFoundError):
        # Git not available or not a git repository
        commit_id = "unknown"
        branch = "unknown"
    
    return commit_id, branch

def get_next_build_number():
    """Get and increment the build number."""
    # CI builds both hardware variants separately. Allow the release workflow
    # to pin one monotonic number so V1 and V2 from the same release report the
    # same build instead of incrementing once per PlatformIO environment.
    build_number_override = os.environ.get("SMART_GRIND_BUILD_NUMBER", "").strip()
    if build_number_override:
        try:
            build_number = int(build_number_override)
            if build_number < 1:
                raise ValueError
            return build_number
        except ValueError:
            print(
                "Warning: SMART_GRIND_BUILD_NUMBER must be a positive integer; "
                "using the local build counter instead",
                file=sys.stderr,
            )

    if platformio_mode:
        project_root = env.get("PROJECT_DIR", os.getcwd())
    else:
        project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    build_number_file = os.path.join(project_root, ".build_number")
    
    # Reuse the local number by default so a no-change build remains truly
    # incremental. CI/release workflows provide SMART_GRIND_BUILD_NUMBER.
    # Developers can explicitly request a new local identity when needed.
    build_number = 1
    if os.path.exists(build_number_file):
        try:
            with open(build_number_file, 'r') as f:
                build_number = int(f.read().strip())
                if os.environ.get("SMART_GRIND_INCREMENT_BUILD", "").strip() == "1":
                    build_number += 1
        except (ValueError, IOError):
            build_number = 1
    
    # Write incremented build number
    try:
        with open(build_number_file, 'w') as f:
            f.write(str(build_number))
    except IOError as e:
        print(f"Warning: Could not write build number: {e}", file=sys.stderr)
    
    return build_number

def create_git_info_header():
    """Create a header file with Git and build information."""
    commit_id, branch = get_git_info()
    
    # Get unique auto-incrementing build number
    build_number = get_next_build_number()
    
    # Generate timestamp for reference
    from datetime import datetime
    build_timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    if platformio_mode:
        project_root = env.get("PROJECT_DIR", os.getcwd())
    else:
        project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    # Keep generated build metadata out of the global include directory.
    # Rewriting a header under include/ makes PlatformIO invalidate every
    # framework and library object, turning incremental builds into full builds.
    header_path = os.path.join(project_root, "src", "config", "git_info.h")

    # Do not rewrite identical metadata on every PlatformIO invocation. A
    # touched generated header invalidates its consumers even when no source
    # changed, defeating incremental builds. CI/release builds pass an explicit
    # SMART_GRIND_BUILD_NUMBER, so release identity remains deterministic.
    if os.path.exists(header_path):
        try:
            with open(header_path, 'r') as existing_file:
                existing = existing_file.read()
            expected_markers = (
                f'#define GIT_COMMIT_ID "{commit_id}"',
                f'#define GIT_BRANCH "{branch}"',
                f'#define BUILD_NUMBER {build_number}',
            )
            if all(marker in existing for marker in expected_markers):
                print(f"Reusing unchanged {header_path} (Build #{build_number})")
                return
        except IOError:
            pass
    
    header_content = f"""#pragma once

// Auto-generated build information - DO NOT EDIT MANUALLY
// Generated by tools/pre_build.py

#define GIT_COMMIT_ID "{commit_id}"
#define GIT_BRANCH "{branch}"
#define BUILD_NUMBER {build_number}
#define BUILD_TIMESTAMP "{build_timestamp}"
"""
    
    try:
        with open(header_path, 'w') as f:
            f.write(header_content)
        print(f"Generated {header_path} (Build #{build_number})")
    except Exception as e:
        print(f"Error creating header file: {e}", file=sys.stderr)

def main():
    """Main function to handle different modes."""
    if len(sys.argv) > 1 and sys.argv[1] == "--header":
        # Generate header file mode (manual call)
        create_git_info_header()
    else:
        # PlatformIO pre-build: generate header AND output flags
        create_git_info_header()
        commit_id, branch = get_git_info()
        print(f"'-DGIT_COMMIT_ID=\"{commit_id}\"'")
        print(f"'-DGIT_BRANCH=\"{branch}\"'")

# When run from PlatformIO, execute immediately
if platformio_mode:
    create_git_info_header()

# When run manually
if __name__ == "__main__":
    main()
