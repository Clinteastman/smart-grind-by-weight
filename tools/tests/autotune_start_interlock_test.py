"""Check production tuning startup rejects conflicts before changing state."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from autotune_cancel_test import function

ROOT = Path(__file__).resolve().parents[2]


class AutoTuneStartInterlockTest(unittest.TestCase):
    def test_start_reservation(self):
        source = (ROOT / "src/controllers/autotune_controller.cpp").read_text()
        method = function(source, "bool AutoTuneController::start()")
        harness = r'''
#include "system/operation_interlock.h"
#include <cassert>
#include <cstring>
#define LOG_BLE(...) ((void)0)
constexpr float GRIND_AUTOTUNE_LATENCY_MIN_MS = 30, GRIND_AUTOTUNE_LATENCY_MAX_MS = 300;
unsigned millis() { return 1; }
enum class AutoTunePhase { IDLE, PRIMING };
struct WeightSensor {} sensor;
struct Grinder {} grinder;
struct GrindController { float get_motor_response_latency() { return 75; } } controller;
struct File {
    explicit operator bool() const { return true; }
    template<class... Args> void println(Args...) { assert(!operation_interlock().try_acquire()); }
    template<class... Args> void printf(Args...) { assert(!operation_interlock().try_acquire()); }
    void flush() { assert(!operation_interlock().try_acquire()); }
};
struct {
    unsigned removes = 0, opens = 0;
    void remove(const char*) { assert(!operation_interlock().try_acquire()); ++removes; }
    File open(const char*, const char*) { assert(!operation_interlock().try_acquire()); ++opens; return {}; }
} LittleFS;
class AutoTuneController {
public:
    WeightSensor* weight_sensor = &sensor;
    Grinder* grinder = &::grinder;
    GrindController* grind_controller = &controller;
    bool is_running = false, cancel_requested = true, found_lower_bound = false;
    float current_pulse_ms = 0, step_size = 0, last_success_ms = 0;
    float active_pulse_ms = 0, last_executed_pulse_ms = 0, candidate_ms = 0;
    enum { UP, DOWN } direction = UP;
    int iteration = 0, verification_round = 0, verification_pulse_count = 0, verification_success_count = 0;
    struct { AutoTunePhase phase; bool has_new_message; float previous_latency_ms; } progress{};
    File autotune_log_file;
    OperationInterlock::Token operation_token = 0;
    unsigned phase_changes = 0;
    void switch_phase(AutoTunePhase phase) { assert(phase == AutoTunePhase::PRIMING); ++phase_changes; }
    bool start();
};
''' + method + r'''
int main() {
    AutoTuneController tuning;
    auto competitor = operation_interlock().try_acquire();
    assert(!tuning.start());
    assert(!tuning.is_running && tuning.cancel_requested && tuning.operation_token == 0);
    assert(LittleFS.opens == 0 && LittleFS.removes == 0 && tuning.phase_changes == 0);
    assert(operation_interlock().owns(competitor));
    operation_interlock().release(competitor);
    tuning.weight_sensor = nullptr;
    assert(!tuning.start());
    auto available = operation_interlock().try_acquire();
    assert(available); operation_interlock().release(available);
    tuning.weight_sensor = &sensor;
    assert(tuning.start());
    assert(tuning.is_running && !tuning.cancel_requested && tuning.phase_changes == 1);
    assert(operation_interlock().owns(tuning.operation_token));
    assert(LittleFS.opens == 1 && LittleFS.removes == 1);
    assert(!tuning.start() && LittleFS.opens == 1 && tuning.phase_changes == 1);
    assert(operation_interlock().release(tuning.operation_token));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "start.cpp", Path(tmp) / "start"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            "-I", str(ROOT / "src"), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
