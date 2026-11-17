#!/usr/bin/env bash
# Build and run the desktop simulator on macOS or Linux (SDL2 window).
#
#   sim/run.sh          build the simulator and open its window
#   sim/run.sh --test   build everything and run the simulator test suite
#
# Windows uses sim/run.ps1 and sim/build.ps1 instead.
set -euo pipefail

sim_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(dirname "$sim_dir")"
build_dir="$sim_dir/build"

# Keep FetchContent state isolated by default so parallel worktrees cannot
# modify the same checkout. Developers can opt into a shared directory.
fetchcontent_base="${SMART_GRIND_FETCHCONTENT_BASE_DIR:-$build_dir/_fetchcontent}"

configure_args=(
    -S "$sim_dir"
    -B "$build_dir"
    -DCMAKE_BUILD_TYPE=Release
    "-DFETCHCONTENT_BASE_DIR=$fetchcontent_base"
)

# Reuse the exact LVGL version a firmware build already installed, so the
# simulator needs no second download.
lvgl_candidates=()
if [[ -n "${SMART_GRIND_LVGL_SOURCE:-}" ]]; then
    lvgl_candidates+=("$SMART_GRIND_LVGL_SOURCE")
fi
lvgl_candidates+=(
    "$project_root/.pio/libdeps/waveshare-esp32s3-touch-amoled-164/lvgl"
    "$project_root/.pio/libdeps/waveshare-esp32s3-touch-amoled-164-v2/lvgl"
)

lvgl_source=""
for candidate in "${lvgl_candidates[@]}"; do
    if [[ -f "$candidate/CMakeLists.txt" && -f "$candidate/library.json" ]] &&
        grep -Eq '"version"[[:space:]]*:[[:space:]]*"9\.5\.0"' "$candidate/library.json"; then
        lvgl_source="$candidate"
        break
    fi
done

if [[ -n "$lvgl_source" ]]; then
    echo "Reusing firmware LVGL source: $lvgl_source"
    configure_args+=("-DFETCHCONTENT_SOURCE_DIR_LVGL=$lvgl_source")
else
    echo "Using LVGL download cache: $fetchcontent_base"
fi

jobs="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

cmake "${configure_args[@]}"

if [[ "${1:-}" == "--test" ]]; then
    # Build every registered test executable before invoking CTest.
    cmake --build "$build_dir" --parallel "$jobs"
    ctest --test-dir "$build_dir" --output-on-failure
    exit
fi

cmake --build "$build_dir" --target smart-grind-sim --parallel "$jobs"
exec "$build_dir/smart-grind-sim"
