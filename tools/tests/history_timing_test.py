"""Run production logger finalization against a controlled clock."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]


class HistoryTimingTest(unittest.TestCase):
    def test_capture_time_not_save_time(self):
        source = (ROOT / "src/logging/grind_logging.cpp").read_text()
        header = "\n".join(line for line in
                           (ROOT / "src/logging/grind_logging.h").read_text().splitlines()
                           if not line.startswith(("#include", "#pragma once")))
        methods = "\n".join(function(source, sig) for sig in (
            "GrindTerminationReason classify_termination_reason(",
            "void GrindLogger::end_grind_session(",
            "void GrindLogger::log_continuous_measurement("))
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include "src/controllers/grind_session.h"
#define SYS_TASK_GRIND_CONTROL_INTERVAL_MS 20
#define SYS_LOG_EVERY_N_GRIND_LOOPS 1
#define GRIND_TIMEOUT_SEC 60
#define ENABLE_GRIND_DEBUG 0
template<class... T> void ignore_log(T...) {}
#define LOG_BLE(...) ignore_log(__VA_ARGS__)
struct Preferences {
    bool begin(const char*, bool) { return true; }
    bool getBool(const char*, bool value) { return value; }
    void end() {}
};
uint32_t clock_ms = 0;
uint32_t millis() { return clock_ms; }
struct Statistics {
    void update_grind_session(float, float, uint8_t, bool, uint32_t) {}
} statistics_manager;
#define private public
''' + header + r'''
#undef private
void GrindLogger::clear_buffers() {}
bool GrindLogger::flush_session_to_flash() { return true; }
''' + methods + r'''
int main() {
    for (uint32_t delay : {0u, 100u, 60000u}) {
        for (uint32_t start : {0u, 1000u, UINT32_MAX - 200u}) {
            for (const char* result : {"COMPLETE", "TIMEOUT"}) {
                GrindSession session{};
                session.grind_mode = static_cast<uint8_t>(GrindMode::TIME);
                session.target_time_ms = 500;
                GrindMeasurement measurements[4];
                GrindLogger logger{};
                logger.current_session = &session;
                logger.measurement_buffer = measurements;
                logger.logging_active = true;
                logger.session_start_time = start;
                clock_ms = start;
                logger.log_continuous_measurement(0, 0, 0, 0, 1, 0, 0);
                clock_ms = start + 300u;
                logger.log_continuous_measurement(300, 0, 0, 0, 0, 0, 0);
                clock_ms = start + 400u;
                logger.log_continuous_measurement(400, 0, 0, 0, 1, 0, 0);
                const uint32_t completed = start + 600u;
                clock_ms = completed + delay;
                logger.end_grind_session(result, 18, 2, completed);
                assert(session.total_time_ms == 600);
                assert(session.total_motor_on_time_ms == 500);
                assert(session.time_error_ms == 0);
                assert(!logger.logging_active);
            }
        }
    }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "timing.cpp", Path(tmp) / "timing"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            "-fsanitize=address,undefined", "-I", str(ROOT),
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
