"""Deterministic interleavings using production controller entry points."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


class ControllerSerializationTest(unittest.TestCase):
    def test_stop_snapshot_and_suspension(self):
        source = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        header = (ROOT / "src/controllers/grind_controller.h").read_text()
        tasks = (ROOT / "src/tasks/task_manager.cpp").read_text()
        update = function(source, "void GrindController::update()")
        prefix = update[update.index("{") + 1:update.index("unsigned long now")]
        # Execute the actual restart-capable PRIME block, preceded by the real
        # update entry guard. Other strategies/peripherals are outside this test.
        prime = function(update, "case GrindPhase::PRIME:")
        events = (ROOT / "src/controllers/grind_events.h").read_text()
        phase_enum = re.search(r"enum class GrindPhase \{.*?\};", header + events, re.S).group()
        lock = function(header, "std::unique_lock<std::recursive_mutex> lock_control() const")
        snapshot = function(header, "GrindSessionDescriptor get_session_descriptor() const")
        methods = "\n".join(function(source, signature) for signature in (
            "void GrindController::stop_grind()", "bool GrindController::is_active() const",
        ))
        suspend = function(tasks, "void TaskManager::suspend_hardware_tasks()")
        resume = function(tasks, "void TaskManager::resume_hardware_tasks()")
        harness = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>
#include "src/controllers/grind_session.h"
#include "src/system/operation_interlock.h"
#define LOG_BLE(...) ((void)0)
constexpr int GRIND_PURGE_MODE_DEFAULT = 1;
constexpr float GRIND_PURGE_AMOUNT_DEFAULT_G = 1;
constexpr unsigned long GRIND_PRIME_MAX_DURATION_MS = 5000;
unsigned long millis() { return 100; }
enum class GrinderPurgeMode { PRIME, PURGE };
struct GrindLoopData { float current_weight = 0; unsigned long now = 100; };
struct FlashOpRequest { enum { UPDATE_MANUAL_RUNTIME } operation_type; uint32_t motor_runtime_ms; };
struct Logger { void discard_current_session() {} } grind_logger;
struct Strategy { void on_exit(const GrindSessionDescriptor&, int) {} };
struct Grinder {
    std::atomic<bool> motor{false};
    std::atomic<bool> gate{false};
    std::promise<void> entered;
    std::shared_future<void> release;
    bool is_grinding() {
        if (gate.exchange(false)) { entered.set_value(); release.wait(); }
        return motor;
    }
    void start() { motor = true; }
    void stop() { motor = false; }
};
''' + phase_enum + r'''
class GrindController {
public:
    mutable std::recursive_mutex control_mutex_;
    OperationInterlock::Token operation_token_ = operation_interlock().try_acquire();
    Grinder* grinder = nullptr;
    GrindMode mode = GrindMode::WEIGHT;
    GrindPhase phase = GrindPhase::PRIME;
    uint32_t time_grind_start_ms = 1, target_time_ms = 1000;
    unsigned long phase_start_time = 1;
    GrinderPurgeMode grinder_purge_mode_for_session{};
    float grinder_purge_amount_g_for_session = 1;
    char last_error_message[32]{};
    Strategy* active_strategy = nullptr;
    GrindSessionDescriptor session_descriptor{};
    int strategy_context = 0;
    void queue_flash_operation(const FlashOpRequest&) {}
    template<class... Args> void queue_log_message(Args...) {}
    void switch_phase(GrindPhase next, const GrindLoopData& = {}) { phase = next; }
    void stop_grind();
    void return_to_idle() { phase = GrindPhase::IDLE; }
    bool is_active() const;
''' + lock + "\n" + snapshot + r'''
    void update() {
''' + prefix + r'''
        GrindLoopData loop_data{};
        switch (phase) {
''' + prime + r'''
            default: break;
        }
    }
};
''' + methods + r'''
std::atomic<unsigned> suspended{0}, resumed{0};
void esp_task_wdt_delete(int) {}
void esp_task_wdt_add(int) {}
void vTaskSuspend(int) { ++suspended; }
void vTaskResume(int) { ++resumed; }
struct TaskManager {
    GrindController* grind_controller;
    bool ota_suspended = false;
    struct { int weight_sampling_task = 1, grind_control_task = 2, file_io_task = 3; } task_handles;
    void suspend_hardware_tasks();
    void resume_hardware_tasks();
};
''' + suspend + "\n" + resume + r'''
int main() {
    using namespace std::chrono_literals;
    GrindController controller;
    Grinder motor; controller.grinder = &motor;
    std::promise<void> release_update;
    motor.release = release_update.get_future().share(); motor.gate = true;
    auto updater = std::async(std::launch::async, [&] { controller.update(); });
    motor.entered.get_future().wait(); // update has reached its would-start decision
    std::promise<void> stop_entered;
    auto stopper = std::async(std::launch::async, [&] {
        stop_entered.set_value(); controller.stop_grind();
    });
    stop_entered.get_future().wait();
    assert(stopper.wait_for(20ms) == std::future_status::timeout);
    release_update.set_value(); updater.get(); stopper.get();
    assert(!motor.motor && !controller.is_active());
    auto other_operation = operation_interlock().try_acquire();
    assert(other_operation);
    motor.motor = true;
    controller.stop_grind();
    assert(motor.motor && operation_interlock().owns(other_operation));
    motor.motor = false;
    operation_interlock().release(other_operation);
    controller.update(); // a later tick must not resurrect the stopped motor
    assert(!motor.motor);

    // Snapshot is an owned copy, not a reference invalidated by the next start.
    static_assert(!std::is_reference_v<decltype(controller.get_session_descriptor())>);
    auto session = controller.get_session_descriptor(); session.target_weight = 99;
    assert(session.target_weight == 99 && controller.get_session_descriptor().target_weight != 99);
    auto owner = controller.lock_control();
    auto reader = std::async(std::launch::async, [&] { return controller.get_session_descriptor(); });
    assert(reader.wait_for(20ms) == std::future_status::timeout);
    controller.session_descriptor.target_weight = 18;
    controller.session_descriptor.target_time_ms = 5000;
    owner.unlock();
    auto coherent = reader.get();
    assert(coherent.target_weight == 18 && coherent.target_time_ms == 5000);

    // Suspending a task that owns the controller would strand all UI getters.
    TaskManager manager{}; manager.grind_controller = &controller;
    owner = controller.lock_control();
    auto suspender = std::async(std::launch::async, [&] { manager.suspend_hardware_tasks(); });
    assert(suspender.wait_for(20ms) == std::future_status::timeout && suspended == 0);
    owner.unlock(); suspender.get();
    assert(suspended == 3 && manager.ota_suspended);
    controller.get_session_descriptor(); // suspension left no controller lock behind
    manager.suspend_hardware_tasks(); assert(suspended == 3);
    manager.resume_hardware_tasks(); manager.resume_hardware_tasks();
    assert(resumed == 3 && !manager.ota_suspended);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "controller.cpp"
            binary = Path(directory) / "controller"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-pthread",
                            "-fsanitize=undefined", "-I", str(ROOT), str(cpp),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)

    def test_public_mutation_boundaries_are_locked(self):
        source = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        for signature in (
            "bool GrindController::start_grind(", "void GrindController::update()",
            "void GrindController::stop_grind()", "void GrindController::return_to_idle()",
            "void GrindController::continue_from_purge()", "void GrindController::pause_grind()",
            "void GrindController::resume_grind()", "void GrindController::start_additional_pulse()",
            "void GrindController::process_queued_flash_operations()",
            "void GrindController::ui_acknowledge_phase_transition()",
        ):
            body = function(source, signature).split("{", 1)[1]
            self.assertTrue(body.lstrip().startswith("const auto control_lock = lock_control();"), signature)


if __name__ == "__main__":
    unittest.main()
