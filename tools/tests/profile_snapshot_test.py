"""Compile the complete production profile controller against fake preferences."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ProfileSnapshotTest(unittest.TestCase):
    def test_concurrent_profile_snapshots(self):
        header = (ROOT / "src/controllers/profile_controller.h").read_text()
        source = (ROOT / "src/controllers/profile_controller.cpp").read_text()
        # Replace only platform includes with test doubles. All controller
        # definitions and method implementations below are production code.
        header = re.sub(r'^#(?:include|pragma).*$', '', header, flags=re.M)
        source = re.sub(r'^#include.*$', '', source, flags=re.M)
        harness = r'''
#include <cassert>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include "src/controllers/grind_mode.h"
constexpr int USER_PROFILE_COUNT = 3, USER_PROFILE_NAME_MAX_LENGTH = 16;
constexpr float USER_SINGLE_ESPRESSO_WEIGHT_G = 9, USER_DOUBLE_ESPRESSO_WEIGHT_G = 18;
constexpr float USER_CUSTOM_PROFILE_WEIGHT_G = 20, USER_SINGLE_ESPRESSO_TIME_S = 5;
constexpr float USER_DOUBLE_ESPRESSO_TIME_S = 10, USER_CUSTOM_PROFILE_TIME_S = 15;
constexpr float USER_MIN_TARGET_WEIGHT_G = 1, USER_MAX_TARGET_WEIGHT_G = 100;
constexpr float USER_MIN_TARGET_TIME_S = 1, USER_MAX_TARGET_TIME_S = 60;
struct Preferences {
    std::map<std::string, int> integers;
    std::map<std::string, float> floats;
    int writes = 0, fail_at = -1;
    int getInt(const char* key, int fallback) {
        const auto it = integers.find(key); return it == integers.end() ? fallback : it->second;
    }
    float getFloat(const char* key, float fallback) {
        const auto it = floats.find(key); return it == floats.end() ? fallback : it->second;
    }
    unsigned putInt(const char* key, int value) {
        std::this_thread::yield(); if (++writes == fail_at) return 0;
        integers[key] = value; return sizeof(int32_t);
    }
    unsigned putFloat(const char* key, float value) {
        std::this_thread::yield(); if (++writes == fail_at) return 0;
        floats[key] = value; return sizeof(float);
    }
};
''' + header + source + r'''
int main() {
    Preferences prefs;
    ProfileController controller; controller.init(&prefs);
    const float weights_a[] = {9, 18, 27}, times_a[] = {3, 6, 9};
    const float weights_b[] = {10, 20, 30}, times_b[] = {4, 8, 12};
    assert(controller.apply_web_settings(0, GrindMode::WEIGHT, weights_a, times_a));
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (unsigned i = 0; i < 10000; ++i) {
            assert(controller.apply_web_settings(2, GrindMode::TIME, weights_b, times_b));
            assert(controller.apply_web_settings(0, GrindMode::WEIGHT, weights_a, times_a));
        }
        done = true;
    });
    unsigned reads = 0;
    do {
        const auto value = controller.snapshot();
        const bool a = value.current_profile == 0;
        assert(value.current_profile == 0 || value.current_profile == 2);
        assert(value.mode == (a ? GrindMode::WEIGHT : GrindMode::TIME));
        for (int i = 0; i < 3; ++i) {
            assert(value.profiles[i].weight == (a ? weights_a[i] : weights_b[i]));
            assert(value.profiles[i].time_seconds == (a ? times_a[i] : times_b[i]));
        }
        ++reads;
    } while (!done);
    writer.join(); assert(reads > 0);
    static_assert(!std::is_reference_v<decltype(controller.snapshot())>);
    auto copy = controller.snapshot(); copy.profiles[0].weight = 99;
    assert(copy.profiles[0].weight != controller.snapshot().profiles[0].weight);
    assert(!controller.apply_web_settings(3, GrindMode::TIME, weights_b, times_b));
    controller.update_current_weight(12); controller.update_current_time(7);
    assert(controller.get_current_weight() == 12 && controller.get_current_time() == 7);
    controller.set_current_profile(1); assert(controller.get_current_profile() == 1);
    controller.set_profile_weight(1, 19); controller.set_profile_time(1, 11);
    assert(controller.get_profile_weight(1) == 19 && controller.get_profile_time(1) == 11);
    assert(std::strcmp(controller.get_current_name(), "DOUBLE") == 0);

    // Inject failure into each of the eight separate NVS writes. The failure
    // must be visible, and RAM must match a fresh boot from the partial store.
    for (int fail = 1; fail <= 8; ++fail) {
        Preferences store;
        ProfileController live; live.init(&store);
        assert(live.apply_web_settings(0, GrindMode::WEIGHT, weights_a, times_a));
        store.writes = 0; store.fail_at = fail;
        assert(!live.apply_web_settings(2, GrindMode::TIME, weights_b, times_b));
        assert(store.writes == 8);
        ProfileController rebooted; rebooted.init(&store);
        const auto current = live.snapshot(), boot = rebooted.snapshot();
        assert(current.current_profile == boot.current_profile && current.mode == boot.mode);
        for (int i = 0; i < 3; ++i) {
            assert(current.profiles[i].weight == boot.profiles[i].weight);
            assert(current.profiles[i].time_seconds == boot.profiles[i].time_seconds);
        }
        store.fail_at = -1;
        assert(live.apply_web_settings(2, GrindMode::TIME, weights_b, times_b));
        assert(live.snapshot().current_profile == 2);
        store.writes = 0;
        assert(!live.apply_web_settings(3, GrindMode::TIME, weights_b, times_b));
        assert(store.writes == 0);
    }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "profiles.cpp", Path(tmp) / "profiles"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-pthread",
                            "-fsanitize=address,undefined", "-I", str(ROOT), str(cpp),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
