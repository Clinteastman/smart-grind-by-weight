"""Host tests for the real display mailbox and controller delivery methods."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class UIEventDeliveryTest(unittest.TestCase):
    def test_mailbox_and_delivery(self):
        source = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        methods = "\n".join(function(source, name) for name in (
            "void GrindController::emit_ui_event(",
            "void GrindController::process_queued_ui_events(",
        ))
        ui = (ROOT / "src/ui/controllers/grinding_controller.cpp").read_text()
        # Exercise the actual phase-entry predicate, including a restart while
        # the previous GRINDING screen is still displayed.
        begin = ui.index("} else if (event_data.phase != GrindPhase::IDLE") + len("} else if (")
        end = ui.index(" {", begin)
        entry_condition = ui[begin:end].rstrip()[:-1]
        harness = r'''
#include "src/controllers/grind_ui_events.h"
#include <atomic>
#include <cassert>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#define portENTER_CRITICAL(mux) (mux)->lock()
#define portEXIT_CRITICAL(mux) (mux)->unlock()
class GrindController {
public:
    GrindUIEventMailbox ui_events_;
    std::mutex ui_event_lock_;
    std::function<void(const GrindEventData&)> ui_event_callback;
    void emit_ui_event(const GrindEventData& data);
    void process_queued_ui_events();
};
''' + methods + r'''
enum class UIState { READY, GRINDING };
struct StateMachine {
    UIState state;
    bool is_state(UIState candidate) { return state == candidate; }
};
struct UIManager { StateMachine* state_machine; };
bool enter_screen(UIManager* ui_manager_, const GrindEventData& event_data) {
    return ''' + entry_condition + r''';
}
GrindEventData event(UIGrindEvent kind, GrindPhase phase, float weight = 0) {
    GrindEventData value{};
    value.event = kind;
    value.phase = phase;
    value.mode = GrindMode::WEIGHT;
    value.current_weight = weight;
    return value;
}
int main() {
    static_assert(static_cast<int>(GrindPhase::PURGE_CONFIRM) == 17);
    static_assert(static_cast<int>(GrindPhase::COMPLETED) == 13);
    GrindController controller;
    std::vector<GrindEventData> received;
    auto capture = [&](const GrindEventData& value) { received.push_back(value); };
    controller.ui_event_callback = capture;
    auto phase = event(UIGrindEvent::PHASE_CHANGED, GrindPhase::PREDICTIVE);
    controller.emit_ui_event(phase);
    for (int i = 0; i < 10000; ++i)
        controller.emit_ui_event(event(UIGrindEvent::PROGRESS_UPDATED, phase.phase, i));
    controller.process_queued_ui_events();
    assert(received.size() == 2);
    assert(received[0].event == UIGrindEvent::PHASE_CHANGED);
    assert(received[1].current_weight == 9999);
    received.clear();
    controller.process_queued_ui_events();
    assert(received.empty());
    controller.emit_ui_event(event(UIGrindEvent::PROGRESS_UPDATED, phase.phase, 10000));
    controller.process_queued_ui_events();
    assert(received.size() == 1 && received[0].current_weight == 10000);

    // A final state cannot be displaced by any number of readings/backgrounds.
    for (auto kind : {UIGrindEvent::COMPLETED, UIGrindEvent::TIMEOUT, UIGrindEvent::STOPPED}) {
        received.clear();
        controller.emit_ui_event(phase);
        controller.emit_ui_event(event(UIGrindEvent::PROGRESS_UPDATED, phase.phase, 17));
        auto terminal = event(kind, kind == UIGrindEvent::STOPPED ? GrindPhase::IDLE :
            kind == UIGrindEvent::TIMEOUT ? GrindPhase::TIMEOUT : GrindPhase::COMPLETED);
        terminal.final_weight = 18.25f;
        std::strcpy(terminal.error_message, "Scale disconnected");
        controller.emit_ui_event(terminal);
        std::strcpy(terminal.error_message, "Overwritten source");
        for (int i = 0; i < 10000; ++i) {
            controller.emit_ui_event(event(UIGrindEvent::PROGRESS_UPDATED, phase.phase, i));
            controller.emit_ui_event(event(UIGrindEvent::PROGRESS_UPDATED, terminal.phase, i));
            auto background = event(UIGrindEvent::BACKGROUND_CHANGE, GrindPhase::IDLE);
            background.background_active = (i != 9999);
            controller.emit_ui_event(background);
        }
        controller.process_queued_ui_events();
        assert(received.size() == 2 && received[0].event == kind);
        assert(received[0].final_weight == 18.25f);
        assert(std::strcmp(received[0].error_message, "Scale disconnected") == 0);
        assert(received[1].event == UIGrindEvent::BACKGROUND_CHANGE);
        assert(!received[1].background_active);
    }

    // More transitions than the old queue could hold: catch up to current state.
    received.clear();
    for (int i = 0; i < 50; ++i) {
        controller.emit_ui_event(phase);
        controller.emit_ui_event(event(UIGrindEvent::STOPPED, GrindPhase::IDLE));
    }
    auto initializing = event(UIGrindEvent::PHASE_CHANGED, GrindPhase::INITIALIZING);
    controller.emit_ui_event(initializing);
    controller.emit_ui_event(event(UIGrindEvent::PROGRESS_UPDATED, phase.phase, 999));
    controller.process_queued_ui_events();
    assert(received.size() == 1 && received[0].phase == GrindPhase::INITIALIZING);
    StateMachine machine{UIState::GRINDING};
    UIManager manager{&machine};
    assert(enter_screen(&manager, received[0])); // must reset/ack the new session
    assert(!enter_screen(&manager, phase));
    machine.state = UIState::READY;
    assert(enter_screen(&manager, phase));

    // Registration delay does not discard initialization; callback-produced
    // events do not deadlock or extend this tick's bounded drain.
    received.clear();
    controller.ui_event_callback = {};
    controller.emit_ui_event(initializing);
    controller.process_queued_ui_events();
    controller.ui_event_callback = [&](const GrindEventData& value) {
        capture(value);
        controller.emit_ui_event(event(UIGrindEvent::STOPPED, GrindPhase::IDLE));
    };
    controller.process_queued_ui_events();
    assert(received.size() == 1 && received[0].phase == GrindPhase::INITIALIZING);
    controller.ui_event_callback = capture;
    controller.process_queued_ui_events();
    assert(received.size() == 2 && received[1].event == UIGrindEvent::STOPPED);

    // Concurrent producer/consumer against the actual locking wrappers.
    received.clear();
    std::atomic<bool> done{false};
    std::thread producer([&] {
        controller.emit_ui_event(phase);
        for (int i = 0; i < 50000; ++i)
            controller.emit_ui_event(event(UIGrindEvent::PROGRESS_UPDATED, phase.phase, i));
        controller.emit_ui_event(event(UIGrindEvent::STOPPED, GrindPhase::IDLE));
        done = true;
    });
    do { controller.process_queued_ui_events(); } while (!done);
    producer.join();
    controller.process_queued_ui_events();
    assert(!received.empty() && received.back().event == UIGrindEvent::STOPPED);
    float previous = -1;
    for (const auto& value : received) {
        if (value.event == UIGrindEvent::PROGRESS_UPDATED) {
            assert(value.current_weight > previous);
            previous = value.current_weight;
        }
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source_path = Path(directory) / "ui_events.cpp"
            executable = Path(directory) / "ui_events"
            source_path.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-pthread", "-fsanitize=undefined", "-I", str(ROOT),
                            str(source_path), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
