"""Run actual cancellation and terminal methods without another UI update."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


class AutoTuneCancelTest(unittest.TestCase):
    def test_immediate_stop_and_cleanup(self):
        source = (ROOT / "src/controllers/autotune_controller.cpp").read_text()
        methods = "\n".join(function(source, sig) for sig in (
            "void AutoTuneController::cancel()",
            "void AutoTuneController::complete_with_failure(",
            "void AutoTuneController::complete_with_success(",
            "void AutoTuneController::update()",
        ))
        harness = r'''
#include <cassert>
#include <cstring>
#include "system/operation_interlock.h"
#define LOG_BLE(...) ((void)0)
constexpr float GRIND_MOTOR_RESPONSE_LATENCY_DEFAULT_MS = 50;
enum class AutoTunePhase { IDLE, PRIMING, BINARY_SEARCH, VERIFICATION, COMPLETE_SUCCESS, COMPLETE_FAILURE };
struct Grinder { bool motor = true; unsigned stops = 0; void stop() { motor = false; ++stops; } } motor;
struct FakeFile {
    bool opened = true;
    unsigned closes = 0;
    explicit operator bool() const { return opened; }
    template<class... Args> void println(Args...) { assert(!motor.motor); }
    template<class... Args> void printf(Args...) { assert(!motor.motor); }
    void close() { assert(!motor.motor); opened = false; ++closes; }
};
struct GrindController {
    bool persist = true;
    float latency = 90;
    bool save_motor_latency(float value) {
        assert(!motor.motor);
        if (!persist) return false;
        latency = value; return true;
    }
    float get_motor_response_latency() { return latency; }
} controller;
class AutoTuneController {
public:
    Grinder* grinder = &motor;
    GrindController* grind_controller = &controller;
    bool is_running = true, cancel_requested = false;
    OperationInterlock::Token operation_token = operation_interlock().try_acquire();
    AutoTunePhase current_phase = AutoTunePhase::PRIMING;
    struct { bool success = false; float latency_ms = 0; const char* error_message = nullptr; } result;
    struct { float previous_latency_ms = 50, final_latency_ms = 0; } progress;
    FakeFile autotune_log_file;
    unsigned advances = 0;
    unsigned success_messages = 0;
    void log_message(const char*) { ++success_messages; }
    void update_priming_phase() { ++advances; }
    void update_binary_search_phase() { ++advances; }
    void update_verification_phase() { ++advances; }
    void update_progress() { assert(!motor.motor); }
    void cancel();
    void update();
    void complete_with_failure(const char*);
    void complete_with_success(float);
};
''' + methods + r'''
int main() {
    // No update() between clicking cancel and checking safety/cleanup.
    AutoTuneController tuning;
    tuning.cancel();
    assert(!operation_interlock().owns(tuning.operation_token));
    auto next = operation_interlock().try_acquire();
    assert(next);
    operation_interlock().release(next);
    assert(!motor.motor && !tuning.is_running && !tuning.autotune_log_file.opened);
    assert(tuning.current_phase == AutoTunePhase::COMPLETE_FAILURE);
    assert(std::strcmp(tuning.result.error_message, "Cancelled by user") == 0);
    assert(tuning.autotune_log_file.closes == 1);
    unsigned stops = motor.stops;
    tuning.cancel(); tuning.update();
    assert(motor.stops == stops && tuning.advances == 0);
    // A stale cancel after tuning ended must not stop some later operation.
    motor.motor = true; tuning.cancel(); assert(motor.motor);
    // Every terminal path stops before any log or preference I/O.
    AutoTuneController failed; failed.complete_with_failure("Settling timeout");
    assert(!motor.motor && !failed.is_running && failed.autotune_log_file.closes == 1);
    motor.motor = true;
    AutoTuneController passed; passed.complete_with_success(75);
    assert(!motor.motor && !passed.is_running && passed.result.success);
    assert(passed.autotune_log_file.closes == 1);
    assert(controller.latency == 75 && passed.success_messages == 1);
    motor.motor = true; controller.persist = false;
    AutoTuneController unsaved; unsaved.complete_with_success(120);
    assert(!motor.motor && !unsaved.is_running && !unsaved.result.success);
    assert(unsaved.current_phase == AutoTunePhase::COMPLETE_FAILURE);
    assert(unsaved.result.latency_ms == 75 && controller.latency == 75);
    assert(std::strcmp(unsaved.result.error_message, "Could not save latency") == 0);
    assert(unsaved.autotune_log_file.closes == 1 && unsaved.success_messages == 0);
    auto released = operation_interlock().try_acquire(); assert(released);
    operation_interlock().release(released);
    // Missing log/motor cannot turn cancellation into a crash.
    AutoTuneController absent; absent.grinder = nullptr; absent.autotune_log_file.opened = false;
    absent.cancel(); assert(!absent.is_running);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "cancel.cpp", Path(tmp) / "cancel"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            "-I", str(ROOT / "src"),
                            "-fsanitize=address,undefined", str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
