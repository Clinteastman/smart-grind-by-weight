"""Run the production GaggiMate polling loop against a WebSocket client that cannot start."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/network/gaggimate_status_client.cpp").read_text()
HEADER = (ROOT / "src/network/gaggimate_status_client.h").read_text()

HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <string>
using SemaphoreHandle_t = void*;
using TickType_t = uint32_t;
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) (ms)
int xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return 1; }
int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
uint32_t now_ms = 10000, ticks = 0;
uint32_t millis() { return now_ms; }
struct StopLoop {};
void vTaskDelay(TickType_t delay) {
    now_ms += delay;
    if (++ticks == 500) throw StopLoop{};  // ten simulated seconds at 50 Hz
}
struct { bool is_connected() const { return true; } } network_manager;
using esp_websocket_client_handle_t = void*;

class GaggiMateStatusClient {
public:
''' + "\n".join(line for line in HEADER.splitlines()
                if "constexpr uint32_t" in line and "_MS" in line) + r'''
    SemaphoreHandle_t mutex_ = nullptr;
    esp_websocket_client_handle_t websocket_ = nullptr;
    bool reconnect_requested_ = false;
    std::string connected_host_;
    uint32_t last_success_ms_ = 0;
    uint32_t last_http_poll_ms_ = 0;
    uint32_t last_websocket_start_ms_ = 0;
    int starts = 0, polls = 0;

    void read_configuration(bool& enabled, std::string& host) { enabled = true; host = "gaggimate.local"; }
    static bool is_valid_host(const std::string& host) { return !host.empty(); }
    // The client cannot be created, so websocket_ stays null after every attempt.
    void start_websocket(const std::string&) { ++starts; }
    void poll_http_fallback(const std::string&) { ++polls; }
    void stop_websocket() {}
    void mark_offline_if_stale(uint32_t) {}
    void task_loop();
};
''' + function(SOURCE, "void GaggiMateStatusClient::task_loop()") + r'''

int main() {
    GaggiMateStatusClient client;
    try { client.task_loop(); } catch (const StopLoop&) {}
    // Ten seconds of a client that never starts: one attempt per retry interval.
    assert(client.starts == 4);
    assert(client.polls == 2);
}
'''


class GaggiMateRetryTest(unittest.TestCase):
    def test_failed_websocket_start_is_retried_at_the_reconnect_interval(self):
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / "retry.cpp", Path(folder) / "retry"
            cpp.write_text(HARNESS)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
