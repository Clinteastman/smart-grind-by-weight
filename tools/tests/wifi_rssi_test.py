"""Verify Wi-Fi RSSI level calculations and grinding safety interlock."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WifiRssiTest(unittest.TestCase):
    def test_rssi_level_and_grinding_interlock(self):
        code = r'''
#include <cassert>
#include <cstdint>
#include <iostream>

enum class NetworkState : uint8_t {
    WIFI_DISABLED,
    WIFI_CONNECTING,
    WIFI_CONNECTED
};

enum class UIState {
    READY,
    MENU,
    GRINDING
};

struct MockNetworkManager {
    NetworkState net_state = NetworkState::WIFI_CONNECTED;
    int mock_rssi = -65;
    int rssi_call_count = 0;

    bool is_enabled() const { return net_state != NetworkState::WIFI_DISABLED; }
    bool is_connected() const { return net_state == NetworkState::WIFI_CONNECTED; }
    bool is_icon_menu_only() const { return false; }

    int get_rssi() {
        if (!is_connected()) return 0;
        ++rssi_call_count;
        return mock_rssi;
    }

    int8_t get_signal_level() {
        if (!is_connected()) return 0;
        const int rssi = get_rssi();
        if (rssi > 0 || rssi < -110) return 1;
        if (rssi >= -67) return 4;
        if (rssi >= -75) return 3;
        if (rssi >= -83) return 2;
        return 1;
    }
} network_manager;

struct MockGrindController {
    bool active = false;
    bool is_active() const { return active; }
};

struct MockStateMachine {
    UIState current_state = UIState::READY;
    bool is_state(UIState s) const { return current_state == s; }
};

struct MockReadyScreen {
    bool is_menu_tab(int) const { return false; }
};

struct MockUIManager {
    MockGrindController* grind_controller = nullptr;
    MockStateMachine* state_machine = nullptr;
    MockReadyScreen ready_screen;
    int current_tab = 0;
};

class StatusIndicatorDouble {
public:
    MockUIManager* ui_manager_ = nullptr;
    int last_wifi_level_ = -1;
    int8_t last_wifi_state_ = -1;
    int last_wifi_visible_tab_ = -1;
    uint32_t last_wifi_poll_ms_ = 0;
    int rendered_bar_level = -1;

    void update_wifi_status_icon(uint32_t now) {
        if (!ui_manager_) return;

        // Grinder active safety interlock:
        const bool is_grinding = (ui_manager_->grind_controller && ui_manager_->grind_controller->is_active()) ||
                                 (ui_manager_->state_machine && ui_manager_->state_machine->is_state(UIState::GRINDING));
        if (is_grinding) {
            return;
        }

        const bool is_in_menu = ui_manager_->state_machine &&
                                ui_manager_->state_machine->is_state(UIState::MENU);

        const int8_t network_state = !network_manager.is_enabled()
                                         ? 0
                                         : network_manager.is_connected() ? 2 : 1;

        const bool menu_only = network_manager.is_icon_menu_only();
        const bool is_on_menu_tab = ui_manager_->state_machine &&
                                    ui_manager_->state_machine->is_state(UIState::READY) &&
                                    ui_manager_->ready_screen.is_menu_tab(ui_manager_->current_tab);

        const int8_t effective_state = is_in_menu
                                           ? 0
                                           : (menu_only && !is_on_menu_tab) ? 0 : network_state;
        const int current_tab = ui_manager_->current_tab;

        if (effective_state == 0) {
            last_wifi_state_ = 0;
            last_wifi_visible_tab_ = current_tab;
            rendered_bar_level = 0;
            return;
        }

        const bool state_changed = (effective_state != last_wifi_state_) || (current_tab != last_wifi_visible_tab_);

        if (!state_changed && effective_state == 2 && (now - last_wifi_poll_ms_ < 5000)) {
            return;
        }

        last_wifi_poll_ms_ = now;
        last_wifi_state_ = effective_state;
        last_wifi_visible_tab_ = current_tab;

        if (effective_state == 1) {
            rendered_bar_level = 0;
            last_wifi_level_ = 0;
            return;
        }

        const int8_t level = network_manager.get_signal_level();
        last_wifi_level_ = level;
        rendered_bar_level = level;
    }
};

int main() {
    // 1. Signal level thresholds:
    network_manager.net_state = NetworkState::WIFI_CONNECTED;
    network_manager.mock_rssi = -45; // >= -67
    assert(network_manager.get_signal_level() == 4);

    network_manager.mock_rssi = -67; // boundary
    assert(network_manager.get_signal_level() == 4);

    network_manager.mock_rssi = -70; // -68 to -75
    assert(network_manager.get_signal_level() == 3);

    network_manager.mock_rssi = -75; // boundary
    assert(network_manager.get_signal_level() == 3);

    network_manager.mock_rssi = -80; // -76 to -83
    assert(network_manager.get_signal_level() == 2);

    network_manager.mock_rssi = -83; // boundary
    assert(network_manager.get_signal_level() == 2);

    network_manager.mock_rssi = -85; // < -83
    assert(network_manager.get_signal_level() == 1);

    network_manager.mock_rssi = 10;  // invalid positive
    assert(network_manager.get_signal_level() == 1);

    network_manager.net_state = NetworkState::WIFI_CONNECTING;
    assert(network_manager.get_signal_level() == 0);

    // 2. Grinding safety interlock
    MockGrindController grind;
    MockStateMachine sm;
    MockUIManager ui;
    ui.grind_controller = &grind;
    ui.state_machine = &sm;

    StatusIndicatorDouble indicator;
    indicator.ui_manager_ = &ui;

    // Normal idle update:
    network_manager.net_state = NetworkState::WIFI_CONNECTED;
    network_manager.mock_rssi = -55;
    int calls_before = network_manager.rssi_call_count;
    indicator.update_wifi_status_icon(1000);
    assert(indicator.rendered_bar_level == 4);
    assert(network_manager.rssi_call_count > calls_before);

    // Now start grinding:
    grind.active = true;
    sm.current_state = UIState::GRINDING;
    calls_before = network_manager.rssi_call_count;

    // Call update multiple times while grinding at different timestamps
    indicator.update_wifi_status_icon(2000);
    indicator.update_wifi_status_icon(7000);
    indicator.update_wifi_status_icon(15000);

    // Zero calls to RSSI allowed during active grinding!
    assert(network_manager.rssi_call_count == calls_before);

    // Grinding completes, return to READY:
    grind.active = false;
    sm.current_state = UIState::READY;
    network_manager.mock_rssi = -80; // changed to 2 bars

    indicator.update_wifi_status_icon(20000);
    assert(indicator.rendered_bar_level == 2);
    assert(network_manager.rssi_call_count > calls_before);

    // Steady state throttling:
    calls_before = network_manager.rssi_call_count;
    indicator.update_wifi_status_icon(21000); // only 1s later
    indicator.update_wifi_status_icon(22000); // 2s later
    // Should be throttled (no extra RSSI queries)
    assert(network_manager.rssi_call_count == calls_before);

    indicator.update_wifi_status_icon(25500); // > 5s passed
    // Now it polls again
    assert(network_manager.rssi_call_count > calls_before);

    std::cout << "All Wi-Fi RSSI and grinding interlock tests passed!" << std::endl;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / "wifi_rssi_test.cpp", Path(folder) / "wifi_rssi_test"
            cpp.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            "-fsanitize=address,undefined", str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
