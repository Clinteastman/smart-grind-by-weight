"""Test that Prime (keep coffee) mode and fresh Purge grinds bypass the PRIME pause."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PrimeBypassTest(unittest.TestCase):
    def test_should_purge_and_phase_selection(self):
        source = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        header = (ROOT / "src/controllers/grind_controller.h").read_text()

        # Extract should_purge from source
        start = source.index("bool GrindController::should_purge() const {")
        end = source.index("\n}\n", start) + 3
        should_purge_code = source[start:end].replace("GrindController::", "")

        harness = r'''
#include <cassert>
#include <cstdint>
#include <algorithm>

enum class GrindMode { WEIGHT, TIME, MANUAL };
enum class GrinderPurgeMode { PRIME, PURGE };
enum class GrindPhase { IDLE, TARING, TARE_CONFIRM, PRIME, PRIME_SETTLING, PURGE_CONFIRM, PREDICTIVE };

constexpr float GRIND_FRESHNESS_DEFAULT_HOURS = 8.0f;
constexpr const char* PREF_KEY_GRIND_FRESHNESS_HOURS = "freshness";

uint64_t g_fake_time_us = 100000000ULL; // 100 seconds
uint64_t esp_timer_get_time() { return g_fake_time_us; }

struct Preferences {
    float freshness_hours = 8.0f;
    float getFloat(const char*, float fallback) { return freshness_hours; }
};

struct GrindController {
    GrindMode mode = GrindMode::WEIGHT;
    GrinderPurgeMode grinder_purge_mode_for_session = GrinderPurgeMode::PRIME;
    bool grinder_purged_since_boot = false;
    uint64_t last_purge_runtime_ms = 0;
    Preferences* preferences = nullptr;

''' + should_purge_code + r'''
};

int main() {
    Preferences prefs;
    GrindController c;
    c.preferences = &prefs;

    // 1. PRIME mode (keep coffee) should NEVER purge, regardless of boot state
    c.mode = GrindMode::WEIGHT;
    c.grinder_purge_mode_for_session = GrinderPurgeMode::PRIME;
    c.grinder_purged_since_boot = false;
    assert(!c.should_purge());

    c.grinder_purged_since_boot = true;
    assert(!c.should_purge());

    // 2. TIME / MANUAL mode should never purge
    c.mode = GrindMode::TIME;
    c.grinder_purge_mode_for_session = GrinderPurgeMode::PURGE;
    assert(!c.should_purge());

    c.mode = GrindMode::MANUAL;
    assert(!c.should_purge());

    // 3. PURGE mode on first grind after boot -> should purge
    c.mode = GrindMode::WEIGHT;
    c.grinder_purge_mode_for_session = GrinderPurgeMode::PURGE;
    c.grinder_purged_since_boot = false;
    assert(c.should_purge());

    // 4. PURGE mode after recent grind (within freshness window) -> should NOT purge
    c.grinder_purged_since_boot = true;
    c.last_purge_runtime_ms = (g_fake_time_us / 1000) - 60000; // 1 minute ago
    assert(!c.should_purge());

    // 5. PURGE mode after stale window (e.g. 9 hours ago with 8h threshold) -> should purge
    c.last_purge_runtime_ms = (g_fake_time_us / 1000) - (uint64_t)(9 * 3600 * 1000);
    assert(c.should_purge());

    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / "test.cpp"
            exe = Path(tmp) / "test"
            cpp.write_text(harness)
            build = subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, f"Compile error:\n{build.stderr}")
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, f"Run error:\n{run.stderr}")


if __name__ == "__main__":
    unittest.main()
