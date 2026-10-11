"""Run the production WebSocket route and client list against esp_http_server fakes."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/network/device_api.cpp").read_text()
HTTP_SUPPORT = (ROOT / "src/network/http_support.cpp").read_text()

HARNESS = r'''
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using esp_err_t = int;
using httpd_handle_t = void*;
#define LOG_BLE(...) std::printf(__VA_ARGS__)
uint32_t now_ms = 0;
uint32_t millis() { return now_ms; }

// What each descriptor currently is on the server, as httpd_ws_get_fd_info reports it.
enum httpd_ws_client_info_t { HTTPD_WS_CLIENT_INVALID, HTTPD_WS_CLIENT_HTTP, HTTPD_WS_CLIENT_WEBSOCKET };
std::map<int, httpd_ws_client_info_t> sessions;
httpd_ws_client_info_t httpd_ws_get_fd_info(httpd_handle_t, int fd) {
    const auto found = sessions.find(fd);
    return found == sessions.end() ? HTTPD_WS_CLIENT_INVALID : found->second;
}
std::vector<int> closed;
esp_err_t httpd_sess_trigger_close(httpd_handle_t, int fd) { closed.push_back(fd); return 0; }

class DeviceApi {
public:
    static constexpr size_t MAX_CLIENTS = 4;
    static constexpr uint32_t PUBLISH_INTERVAL_MS = 100;
    static constexpr uint8_t MAX_CONSECUTIVE_BACKPRESSURE_SKIPS = 50;
    static constexpr int NO_CLIENT = -1;
    bool initialized_ = true;
    httpd_handle_t server_ = this;
    std::atomic<bool> settings_cache_dirty_{false};
    uint32_t last_publish_ms_ = 0;
    std::atomic<int> client_fds_[MAX_CLIENTS]{};
    std::atomic<uint8_t> backpressure_skips_[MAX_CLIENTS]{};
    std::vector<int> sent_to;

    DeviceApi() { for (auto& slot : client_fds_) slot.store(NO_CLIENT); }
    void refresh_settings_cache() {}
    std::string build_state_message() { return "{}"; }
    // Records the frame; production send_text writes it to the socket.
    bool send_text(int fd, const std::string&) {
        assert(httpd_ws_get_fd_info(server_, fd) == HTTPD_WS_CLIENT_WEBSOCKET);
        sent_to.push_back(fd);
        return true;
    }
    void update();
    void add_client(int client_fd);
    void remove_client(int client_fd);
    size_t clients() const {
        size_t count = 0;
        for (const auto& slot : client_fds_) count += slot.load() != NO_CLIENT;
        return count;
    }
};
''' + "\n".join(function(SOURCE, signature) for signature in (
    "void DeviceApi::update()",
    "void DeviceApi::add_client(int client_fd)",
    "void DeviceApi::remove_client(int client_fd)",
)) + r'''

void publish(DeviceApi& api) {
    api.sent_to.clear();
    now_ms += DeviceApi::PUBLISH_INTERVAL_MS;
    api.update();
}

int main() {
    DeviceApi api;
    sessions[54] = HTTPD_WS_CLIENT_WEBSOCKET;
    sessions[55] = HTTPD_WS_CLIENT_WEBSOCKET;
    api.add_client(54);
    api.add_client(55);
    publish(api);
    assert((api.sent_to == std::vector<int>{54, 55}));

    // The server purges client 54 without a CLOSE frame and a plain HTTP
    // request receives the same descriptor.
    sessions[54] = HTTPD_WS_CLIENT_HTTP;
    publish(api);
    assert((api.sent_to == std::vector<int>{55}) && api.clients() == 1);
    publish(api);
    assert((api.sent_to == std::vector<int>{55}));

    // A dropped peer whose descriptor is not reused yet is forgotten too.
    sessions.erase(55);
    publish(api);
    assert(api.sent_to.empty() && api.clients() == 0 && closed.empty());

    // A WebSocket that reuses a descriptor still listed occupies one slot.
    sessions[56] = HTTPD_WS_CLIENT_WEBSOCKET;
    api.add_client(56);
    api.add_client(56);
    publish(api);
    assert((api.sent_to == std::vector<int>{56}) && api.clients() == 1);
}
'''

# esp_http_server calls a WebSocket route's handler only for frames. The
# handshake GET reaches it solely through the post-handshake callback.
ROUTE_HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <deque>
#include <functional>
#include <vector>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
using httpd_handle_t = void*;
enum httpd_method_t { HTTP_DELETE = 0, HTTP_GET = 1 };
struct httpd_req_t { int method; void* user_ctx; };
struct httpd_uri_t {
    const char* uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t*);
    void* user_ctx;
    bool is_websocket;
    bool handle_ws_control_frames;
    const char* supported_subprotocol;
    esp_err_t (*ws_post_handshake_cb)(httpd_req_t*);
};
const char* esp_err_to_name(esp_err_t) { return "error"; }
#define LOG_BLE(...) std::printf(__VA_ARGS__)
std::vector<httpd_uri_t> registered;
esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t* uri) {
    registered.push_back(*uri);
    return ESP_OK;
}
using Handler = std::function<esp_err_t(httpd_req_t*)>;
''' + "\n".join(function(HTTP_SUPPORT, signature) for signature in (
    "std::deque<Handler>& handler_store()",
    "esp_err_t dispatch(httpd_req_t* request)",
    "bool register_route(",
)) + r'''

int main() {
    int server = 0;
    std::vector<int> calls;
    assert(register_route(&server, "/ws", HTTP_GET, [&calls](httpd_req_t* request) {
        calls.push_back(request->method);
        return ESP_OK;
    }, true));
    assert(register_route(&server, "/api", HTTP_GET, [](httpd_req_t*) { return ESP_OK; }, false));

    // The server completes a handshake: it calls the post-handshake callback
    // with the GET, and the route's handler sees it exactly once.
    const httpd_uri_t& websocket = registered[0];
    assert(websocket.is_websocket && websocket.ws_post_handshake_cb != nullptr);
    httpd_req_t handshake{HTTP_GET, websocket.user_ctx};
    assert(websocket.ws_post_handshake_cb(&handshake) == ESP_OK);
    assert((calls == std::vector<int>{HTTP_GET}));

    // Plain routes have no handshake.
    assert(!registered[1].is_websocket && registered[1].ws_post_handshake_cb == nullptr);
}
'''

FRAME_HARNESS = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
using httpd_handle_t = void*;
// A frame request's method is never HTTP_GET; only the handshake is.
enum httpd_method_t { HTTP_DELETE = 0, HTTP_GET = 1 };
enum httpd_ws_type_t { HTTPD_WS_TYPE_TEXT = 1, HTTPD_WS_TYPE_CLOSE = 8,
                       HTTPD_WS_TYPE_PING = 9, HTTPD_WS_TYPE_PONG = 10 };
struct httpd_ws_frame_t { bool final; bool fragmented; httpd_ws_type_t type; uint8_t* payload; size_t len; };
struct httpd_req_t {
    int method; int fd; bool cross_origin; std::string text;
    httpd_ws_type_t type = HTTPD_WS_TYPE_TEXT;
    int payload_reads = 0;  // recv_frame calls that consumed the payload
};
int httpd_req_to_sockfd(httpd_req_t* request) { return request->fd; }
esp_err_t httpd_ws_recv_frame(httpd_req_t* request, httpd_ws_frame_t* frame, size_t max_len) {
    frame->type = request->type;
    frame->len = request->text.size();
    if (max_len) {
        if (frame->len > max_len) return ESP_FAIL;
        memcpy(frame->payload, request->text.data(), frame->len);
        ++request->payload_reads;
    }
    return ESP_OK;
}
esp_err_t httpd_ws_send_frame(httpd_req_t*, httpd_ws_frame_t*) { return ESP_OK; }
std::vector<int> closed;
esp_err_t httpd_sess_trigger_close(httpd_handle_t, int fd) { closed.push_back(fd); return ESP_OK; }
namespace http {
bool origin_allowed(httpd_req_t* request) { return !request->cross_origin; }
}
using SemaphoreHandle_t = void*;
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
int xSemaphoreTake(SemaphoreHandle_t, int) { return pdTRUE; }
void xSemaphoreGive(SemaphoreHandle_t) {}
#define LOG_BLE(...) std::printf(__VA_ARGS__)

class DeviceApi {
public:
    static constexpr size_t MAX_CLIENTS = 4;
    static constexpr uint32_t WS_SEND_MUTEX_TIMEOUT_MS = 50;
    static constexpr int NO_CLIENT = -1;
    httpd_handle_t server_ = this;
    SemaphoreHandle_t ws_send_mutex_ = this;
    std::atomic<int> client_fds_[MAX_CLIENTS]{};
    std::atomic<uint8_t> backpressure_skips_[MAX_CLIENTS]{};
    std::vector<std::string> commands;

    DeviceApi() { for (auto& slot : client_fds_) slot.store(NO_CLIENT); }
    std::string build_state_message() { return "{}"; }
    bool send_text(int, const std::string&) { return true; }
    void queue_command(int, const uint8_t* data, size_t len) {
        commands.emplace_back(reinterpret_cast<const char*>(data), len);
    }
    esp_err_t handle_websocket(httpd_req_t* request);
    void add_client(int client_fd);
    void remove_client(int client_fd);
    bool is_client(int client_fd) const;
};
''' + "\n".join(function(SOURCE, signature) for signature in (
    "esp_err_t DeviceApi::handle_websocket(httpd_req_t* request)",
    "void DeviceApi::add_client(int client_fd)",
    "void DeviceApi::remove_client(int client_fd)",
    "bool DeviceApi::is_client(int client_fd) const",
)) + r'''

int main() {
    const std::string start = R"({"type":"command","action":"start"})";
    DeviceApi api;

    // The grinder's own page connects and its command is queued.
    httpd_req_t page{HTTP_GET, 60, false, ""};
    api.handle_websocket(&page);
    httpd_req_t page_start{HTTP_DELETE, 60, false, start};
    api.handle_websocket(&page_start);
    assert(api.commands.size() == 1 && closed.empty());

    // An unsolicited PONG's payload is consumed, as esp_http_server requires,
    // so the next frame header is read from the right place; the client stays.
    httpd_req_t pong{HTTP_DELETE, 60, false, "heartbeat"};
    pong.type = HTTPD_WS_TYPE_PONG;
    assert(api.handle_websocket(&pong) == ESP_OK);
    assert(pong.payload_reads == 1 && api.commands.size() == 1 && closed.empty());

    // A foreign page is refused at the handshake, and a frame it sends
    // regardless never reaches the command queue.
    httpd_req_t foreign{HTTP_GET, 61, true, ""};
    api.handle_websocket(&foreign);
    assert((closed == std::vector<int>{61}));
    httpd_req_t foreign_start{HTTP_DELETE, 61, true, start};
    api.handle_websocket(&foreign_start);
    assert(api.commands.size() == 1 && (closed == std::vector<int>{61, 61}));

    // Nor does a frame from a socket whose handshake the handler never saw.
    httpd_req_t unseen{HTTP_DELETE, 62, false, start};
    api.handle_websocket(&unseen);
    assert(api.commands.size() == 1 && (closed == std::vector<int>{61, 61, 62}));
}
'''


def compile_and_run(harness, name):
    with tempfile.TemporaryDirectory() as folder:
        cpp, binary = Path(folder) / f"{name}.cpp", Path(folder) / name
        cpp.write_text(harness)
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-format",
                        str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10, stdout=subprocess.DEVNULL)


class WebSocketClientsTest(unittest.TestCase):
    def test_closed_sessions_stop_receiving_state_frames(self):
        compile_and_run(HARNESS, "clients")

    def test_handshake_reaches_the_handler(self):
        compile_and_run(ROUTE_HARNESS, "route")
        defaults = (ROOT / "sdkconfig.defaults").read_text()
        self.assertRegex(defaults, r"(?m)^CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT=y$")
        # ESP-IDF 5.5.5 is the first 5.5 release with the post-handshake callback.
        manifest = (ROOT / "src/idf_component.yml").read_text()
        minimum = re.search(r'idf:\s*\n\s*version: ">=([\d.]+)"', manifest).group(1)
        self.assertGreaterEqual(tuple(int(part) for part in minimum.split(".")), (5, 5, 5))

    def test_frames_from_unadmitted_sockets_are_dropped(self):
        compile_and_run(FRAME_HARNESS, "frames")


if __name__ == "__main__":
    unittest.main()
