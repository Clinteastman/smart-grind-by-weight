"""Run production settings writes with injected NVS/dependency failures."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class SettingsPersistenceTest(unittest.TestCase):
    def test_write_failures(self):
        api = (ROOT / "src/network/device_api.cpp").read_text()
        controller = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        header = (ROOT / "src/network/device_api.h").read_text()
        settings = header[header.index("struct DeviceSettingsUpdate"):header.index("class DeviceApi")]
        implementation = "\n".join([
            method(controller, "bool GrindController::save_motor_latency("),
            method(controller, "bool GrindController::save_coast_ratio("),
            method(api, "bool DeviceApi::apply_settings("),
        ])
        harness = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <string>
using String = std::string;
#define LOG_BLE(...) ((void)0)
constexpr float GRIND_AUTOTUNE_LATENCY_MIN_MS = 30, GRIND_AUTOTUNE_LATENCY_MAX_MS = 300;
constexpr float GRIND_LATENCY_TO_COAST_RATIO_MIN = 0.1f, GRIND_LATENCY_TO_COAST_RATIO_MAX = 3;
struct Preferences {
    inline static int writes = 0, fail_write = -1, opens = 0, fail_open = -1;
    inline static std::map<String, String> strings;
    String ns = "grinder";
    bool begin(const char* name, bool) { ns = name; return ++opens != fail_open; }
    void end() {}
    size_t putInt(const char*, int) { return ++writes == fail_write ? 0 : sizeof(int32_t); }
    size_t putFloat(const char*, float) { return ++writes == fail_write ? 0 : sizeof(float); }
    size_t putBool(const char*, bool) { return ++writes == fail_write ? 0 : sizeof(bool); }
    size_t putString(const char* key, const char* value) {
        if (++writes == fail_write) return 0;
        strings[ns + "/" + key] = value; return strlen(value);
    }
    String getString(const char* key, const char* fallback) {
        const auto it = strings.find(ns + "/" + key);
        return it == strings.end() ? fallback : it->second;
    }
    static void reset() { writes = opens = 0; fail_write = fail_open = -1; strings.clear(); }
};
enum class GrindMode { WEIGHT, TIME };
struct GrindController {
    Preferences* preferences;
    std::recursive_mutex mutex;
    auto lock_control() { return std::unique_lock<std::recursive_mutex>(mutex); }
    bool active = false;
    bool is_active() { return active; }
    float motor_response_latency_ms = 50, coast_ratio_ = 1;
    static constexpr const char* PREF_KEY_COAST_RATIO = "coast";
    static constexpr const char* PREF_KEY_GRINDER_MODE = "purge_mode";
    static constexpr const char* PREF_KEY_GRINDER_AMOUNT_G = "purge_amount";
    static constexpr const char* PREF_KEY_GRIND_FRESHNESS_HOURS = "freshness";
    bool save_motor_latency(float);
    bool save_coast_ratio(float);
};
struct ProfileController {
    bool succeed = true;
    bool apply_web_settings(int, GrindMode, const float*, const float*) { return succeed; }
};
struct Display {
    float brightness = 0.5f;
    void set_brightness(float value) { brightness = value; }
};
struct HardwareManager {
    Preferences* prefs;
    Display display;
    Preferences* get_preferences() { return prefs; }
    Display* get_display() { return &display; }
};
namespace ScreensaverSettings {
bool succeed = true;
bool save_timing(uint16_t, uint8_t, bool, uint16_t) { return succeed; }
}
struct StatusClient {
    bool succeed = true, enabled = false;
    bool configure(bool next_enabled, const char*) { enabled = next_enabled; return succeed; }
} gaggimate_status_client;
''' + settings + r'''
struct DeviceApi {
    HardwareManager* hardware_;
    ProfileController* profile_controller_;
    GrindController* grind_controller_;
    bool apply_settings(const DeviceSettingsUpdate&);
};
''' + implementation + r'''
int main() {
    Preferences prefs;
    GrindController control; control.preferences = &prefs;
    HardwareManager hw{&prefs, {}};
    ProfileController profiles;
    DeviceApi api{&hw, &profiles, &control};
    DeviceSettingsUpdate update;
    update.motor_latency_ms = 100; update.coast_ratio = 2;
    strcpy(update.screensaver_style, "gaggimate");
    assert(api.apply_settings(update));
    const int total_writes = Preferences::writes, total_opens = Preferences::opens;
    assert(total_writes == 16 && total_opens == 10);
    assert(control.motor_response_latency_ms == 100 && control.coast_ratio_ == 2);
    for (int failure = 1; failure <= total_writes; ++failure) {
        Preferences::reset(); Preferences::fail_write = failure;
        control.motor_response_latency_ms = 50; control.coast_ratio_ = 1;
        hw.display.brightness = 0.5f;
        assert(!api.apply_settings(update));
        assert(Preferences::writes == total_writes);
        if (failure == 4) assert(control.coast_ratio_ == 1);
        if (failure == 5) assert(control.motor_response_latency_ms == 50);
        if (failure == 14) assert(hw.display.brightness == 0.5f);
        if (failure == 16) assert(!gaggimate_status_client.enabled);
    }
    for (int failure = 1; failure <= total_opens; ++failure) {
        Preferences::reset(); Preferences::fail_open = failure;
        assert(!api.apply_settings(update));
    }
    Preferences::reset();
    profiles.succeed = false; assert(!api.apply_settings(update)); profiles.succeed = true;
    ScreensaverSettings::succeed = false; assert(!api.apply_settings(update));
    ScreensaverSettings::succeed = true;
    gaggimate_status_client.succeed = false; assert(!api.apply_settings(update));
    gaggimate_status_client.succeed = true;
    Preferences::reset(); control.active = true;
    assert(!api.apply_settings(update)); assert(Preferences::writes == 0);
    control.active = false; hw.prefs = nullptr;
    assert(!api.apply_settings(update)); assert(Preferences::writes == 0);
    hw.prefs = &prefs;
    // Both timing setters reject invalid values without writes and leave RAM
    // unchanged when their own persistence fails.
    for (float bad : {std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity(), -1.0f, 1000.0f}) {
        assert(!control.save_motor_latency(bad)); assert(!control.save_coast_ratio(bad));
    }
    assert(Preferences::writes == 0);
    control.preferences = nullptr;
    assert(!control.save_motor_latency(100)); assert(!control.save_coast_ratio(2));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source, binary = Path(tmp) / "settings.cpp", Path(tmp) / "settings"
            source.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-pthread",
                            "-fsanitize=address,undefined", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
