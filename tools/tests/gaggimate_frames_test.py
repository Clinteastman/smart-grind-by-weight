"""Run the production GaggiMate WebSocket event handler against frames the client splits."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/network/gaggimate_status_client.cpp").read_text()
HEADER = (ROOT / "src/network/gaggimate_status_client.h").read_text()

HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
using esp_event_base_t = const char*;
using esp_websocket_client_handle_t = void*;
enum { WEBSOCKET_EVENT_ERROR = 1, WEBSOCKET_EVENT_CONNECTED, WEBSOCKET_EVENT_DISCONNECTED,
       WEBSOCKET_EVENT_DATA, WEBSOCKET_EVENT_CLOSED };
// The fields esp_websocket_client fills for a DATA event.
struct esp_websocket_event_data_t {
    const char* data_ptr;
    int data_len;
    bool fin;
    uint8_t op_code;
    esp_websocket_client_handle_t client;
    void* user_context;
    int payload_len;
    int payload_offset;
};
uint32_t millis() { return 0; }
#define LOG_BLE(...) ((void)0)

class GaggiMateStatusClient {
public:
''' + "\n".join(line for line in HEADER.splitlines()
                if "constexpr size_t" in line and "_BYTES" in line) + r'''
    std::string websocket_frame_;
    std::vector<std::string> applied;
    int offline_checks = 0;
    bool apply_status_payload(const std::string& payload, bool) {
        applied.push_back(payload);
        return true;
    }
    void mark_offline_if_stale(uint32_t) { ++offline_checks; }
    static void websocket_event_handler(void* context, esp_event_base_t base, int32_t id,
                                        void* data);
};
''' + function(SOURCE, "void GaggiMateStatusClient::websocket_event_handler(") + r'''

// esp_websocket_client posts a payload larger than its buffer as several DATA
// events of at most `piece` bytes, each carrying the whole frame's payload_len.
void deliver(GaggiMateStatusClient& client, const std::string& frame, int piece,
             uint8_t op_code = 0x01) {
    for (int offset = 0; offset < static_cast<int>(frame.size()); offset += piece) {
        const int length = std::min<int>(piece, static_cast<int>(frame.size()) - offset);
        esp_websocket_event_data_t event{frame.data() + offset, length, true, op_code,
                                         nullptr, nullptr, static_cast<int>(frame.size()), offset};
        GaggiMateStatusClient::websocket_event_handler(&client, "WEBSOCKET_EVENTS",
                                                       WEBSOCKET_EVENT_DATA, &event);
    }
}

int main() {
    GaggiMateStatusClient client;
    const std::string small = R"({"tp":"evt:status","ct":93.1,"tt":93.0})";
    deliver(client, small, 1024);
    assert((client.applied == std::vector<std::string>{small}));

    // A status frame longer than the client's 1024-byte buffer is parsed once, whole.
    const std::string large = R"({"tp":"evt:status","p":")" + std::string(1500, 'x') +
                              R"(","ct":93.1,"tt":93.0})";
    client.applied.clear();
    deliver(client, large, 1024);
    assert((client.applied == std::vector<std::string>{large}));

    // Binary frames and payloads above the limit are dropped, not parsed.
    client.applied.clear();
    deliver(client, small, 1024, 0x02);
    deliver(client, std::string(GaggiMateStatusClient::WEBSOCKET_FRAME_MAX_BYTES + 1, 'y'), 1024);
    assert(client.applied.empty());

    // The next complete frame after a dropped one still parses.
    deliver(client, small, 1024);
    assert((client.applied == std::vector<std::string>{small}));
}
'''


class GaggiMateFramesTest(unittest.TestCase):
    def test_split_status_frames_are_reassembled(self):
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / "frames.cpp", Path(folder) / "frames"
            cpp.write_text(HARNESS)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
