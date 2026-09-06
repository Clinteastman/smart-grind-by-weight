"""Exercise production completion/dismiss/pulse methods with a bounded fake queue."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]


class SessionCompletionTest(unittest.TestCase):
    def test_completion_lifecycle(self):
        source = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        methods = "\n".join(function(source, sig) for sig in (
            "bool GrindController::queue_terminal_session()",
            "bool GrindController::queue_flash_operation(",
            "void GrindController::process_queued_flash_operations()",
            "void GrindController::return_to_idle()",
            "void GrindController::stop_grind()",
            "void GrindController::start_additional_pulse()",
            "bool GrindController::can_pulse() const",
        ))
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
template<class... Args> void ignore_log(Args...) {}
#define LOG_BLE(...) ignore_log(__VA_ARGS__)
#include "src/controllers/grind_session.h"
#include "src/controllers/grind_session_result.h"
enum class GrindPhase { IDLE, COMPLETED, TIMEOUT, TIME_ADDITIONAL_PULSE };
enum class GrinderPurgeMode { PRIME, PURGE };
constexpr int GRIND_PURGE_MODE_DEFAULT = 1;
constexpr float GRIND_PURGE_AMOUNT_DEFAULT_G = 1;
uint32_t clock_ms = 100;
unsigned long millis() { return clock_ms; }
struct GrindLoopData { unsigned long now = 0; };
struct FlashOpRequest {
    enum Type { START_GRIND_SESSION, END_GRIND_SESSION, UPDATE_MANUAL_RUNTIME } operation_type;
    GrindSessionDescriptor descriptor{};
    char result_string[32]{};
    float start_weight = 0, final_weight = 0;
    uint8_t pulse_count = 0;
    uint32_t motor_runtime_ms = 0;
    uint32_t completed_at_ms = 0;
};
struct Queue { std::deque<FlashOpRequest> requests; unsigned capacity = 1; };
using BaseType_t = int;
constexpr int pdPASS = 1;
int xQueueSend(Queue* q, const FlashOpRequest* r, int) {
    if (q->requests.size() == q->capacity) return 0;
    q->requests.push_back(*r); return pdPASS;
}
int xQueueReceive(Queue* q, FlashOpRequest* r, int) {
    if (q->requests.empty()) return 0;
    *r = q->requests.front(); q->requests.pop_front(); return pdPASS;
}
struct Logger {
    bool active = true;
    unsigned saves = 0, discards = 0, generation = 1;
    std::vector<unsigned> saved_generations;
    std::string result;
    float weight = 0;
    uint32_t completed_at_ms = 0;
    bool is_logging_active() const { return active; }
    void start_grind_session(const GrindSessionDescriptor&, float) { active = true; ++generation; }
    void end_grind_session(const char* r, float w, uint8_t, uint32_t ended) {
        completed_at_ms = ended;
        assert(active); ++saves; active = false; result = r; weight = w;
        saved_generations.push_back(generation);
    }
    void discard_current_session() { active = false; ++discards; }
} grind_logger;
struct Statistics { void update_manual_grind(uint32_t) {} void update_time_pulse() {} } statistics_manager;
struct Grinder {
    bool motor = false;
    void stop() { motor = false; }
    void start_pulse_rmt(uint32_t) { assert(!grind_logger.active); motor = true; }
};
struct Strategy { void on_exit(const GrindSessionDescriptor&, int) {} };
class GrindController {
public:
    mutable std::recursive_mutex mutex;
    auto lock_control() const { return std::unique_lock<std::recursive_mutex>(mutex); }
    GrindPhase phase = GrindPhase::COMPLETED;
    GrindMode mode = GrindMode::TIME;
    GrindSessionResult last_session_result_ = GrindSessionResult::SUCCESS;
    bool session_end_flash_queued = false;
    float final_weight = 18.5f;
    int pulse_attempts = 3;
    uint32_t phase_start_time = 80;
    Queue* flash_op_queue;
    Grinder* grinder;
    uint32_t time_grind_start_ms = 0, target_time_ms = 0, start_time = 0, pulse_duration_ms = 100;
    int additional_pulse_count = 0;
    GrinderPurgeMode grinder_purge_mode_for_session{};
    float grinder_purge_amount_g_for_session = 1;
    char last_error_message[32]{};
    Strategy* active_strategy = nullptr;
    GrindSessionDescriptor session_descriptor{};
    int strategy_context = 0;
    void switch_phase(GrindPhase p, const GrindLoopData& = {}) { phase = p; }
    bool queue_terminal_session();
    bool queue_flash_operation(const FlashOpRequest&);
    void process_queued_flash_operations();
    void return_to_idle();
    void stop_grind();
    void start_additional_pulse();
    bool can_pulse() const;
};
''' + methods + r'''
int main() {
    Queue queue; Grinder motor;
    GrindController c; c.flash_op_queue = &queue; c.grinder = &motor;
    // Notification followed immediately by dismissal, without another update.
    assert(c.queue_terminal_session());
    assert(c.queue_terminal_session() && queue.requests.size() == 1);
    clock_ms = 5000;
    c.return_to_idle();
    assert(grind_logger.completed_at_ms == 80);
    assert(c.phase == GrindPhase::IDLE && grind_logger.saves == 1 && !grind_logger.active);
    c.return_to_idle(); assert(grind_logger.saves == 1);
    // A new session must not be ended by the old queued completion.
    grind_logger.start_grind_session({}, 0);
    c.process_queued_flash_operations();
    assert(grind_logger.active && grind_logger.saves == 1);
    assert(grind_logger.saved_generations[0] == 1);
    // Full queue does not falsely mark completion as queued; dismiss drains/retries.
    c.phase = GrindPhase::TIMEOUT; c.session_end_flash_queued = false;
    FlashOpRequest filler{}; filler.operation_type = FlashOpRequest::UPDATE_MANUAL_RUNTIME;
    assert(c.queue_flash_operation(filler));
    assert(!c.queue_terminal_session() && !c.session_end_flash_queued);
    clock_ms = 9000;
    c.return_to_idle();
    assert(grind_logger.completed_at_ms == 80);
    assert(grind_logger.saves == 2 && grind_logger.result == "TIMEOUT");
    assert(grind_logger.weight == 18.5f && queue.requests.empty());
    // Stop on an already finished session preserves, rather than discards, it.
    grind_logger.start_grind_session({}, 0);
    c.phase = GrindPhase::COMPLETED; c.session_end_flash_queued = false;
    c.last_session_result_ = GrindSessionResult::OVERSHOOT;
    c.stop_grind();
    assert(grind_logger.saves == 3 && grind_logger.discards == 0);
    assert(grind_logger.result == "OVERSHOOT");
    // Extra pulse cannot start until the prior session has been saved.
    grind_logger.start_grind_session({}, 0);
    c.phase = GrindPhase::COMPLETED; c.session_end_flash_queued = false;
    c.start_additional_pulse();
    assert(grind_logger.saves == 4 && motor.motor);
    assert(c.phase == GrindPhase::TIME_ADDITIONAL_PULSE);
    motor.stop(); c.phase = GrindPhase::COMPLETED;
    c.return_to_idle(); assert(grind_logger.saves == 4);
    // Missing queue must neither crash nor release unsaved buffers for reuse.
    grind_logger.start_grind_session({}, 0);
    c.phase = GrindPhase::COMPLETED; c.session_end_flash_queued = false; c.flash_op_queue = nullptr;
    c.return_to_idle(); assert(c.phase == GrindPhase::COMPLETED && grind_logger.active);
    c.start_additional_pulse(); assert(!motor.motor);
    // Logging disabled: no record to preserve and no queue required.
    grind_logger.active = false; c.return_to_idle(); assert(c.phase == GrindPhase::IDLE);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "session.cpp", Path(tmp) / "session"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-pthread",
                            "-fsanitize=address,undefined", "-I", str(ROOT), str(cpp),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)

    def test_completion_is_queued_before_notification(self):
        source = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        switch = function(source, "void GrindController::switch_phase(")
        self.assertLess(switch.index("queue_terminal_session();"), switch.index("emit_ui_event(event_data);"))
        self.assertLess(switch.index("last_session_result_ = session_result;"), switch.index("queue_terminal_session();"))
        update = function(source, "void GrindController::update()")
        self.assertIn("case GrindPhase::TIMEOUT:\n            queue_terminal_session();", update)


if __name__ == "__main__":
    unittest.main()
