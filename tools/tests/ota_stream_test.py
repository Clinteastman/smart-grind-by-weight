"""Run the production release download and manifest fetch against ESP-IDF client fakes.

DeviceWebServer::perform_github_ota is compiled unchanged against a fake of
esp_https_ota that scripts what the release mirror sends, and fetch_text against
a fake esp_http_client_perform that delivers each response's body through the
event handler, as esp_http_client.c does. Reading, timeouts, redirects and
image validation are esp_https_ota's; these tests hold the firmware to its side:
the request it configures, the image it refuses before any flash is erased, the
progress it reports, and exactly one result per download, with the download
handle released exactly once.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/network/device_web_server.cpp").read_text()
PARTITION_SIZE = 0x300000


def constants_used_by(source, code):
    """The production file-scope constexpr definitions that `code` refers to."""
    declarations = re.findall(r"^constexpr\b[^;]*;", source, re.M)
    names = [re.search(r"(\w+)\s*(?:\[\])?\s*=", declaration).group(1)
             for declaration in declarations]
    return [declaration for declaration, name in zip(declarations, names)
            if re.search(rf"\b{name}\b", code)]


PRODUCTION = "\n".join([
    function(SOURCE, "struct TextResponse {") + ";",
    function(SOURCE, "esp_err_t collect_text(esp_http_client_event_t* event)"),
    function(SOURCE, "bool fetch_text("),
    function(SOURCE, "esp_err_t prepare_release_request(esp_http_client_handle_t client)"),
    function(SOURCE, "void DeviceWebServer::perform_github_ota(const std::string& tag)"),
])

HARNESS = r'''
#include "config/constants.h"  // production LOG_BLE and HW_RELEASE_FIRMWARE_SUFFIX

#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

void diagnostic_log_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
}

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_HTTPS_OTA_IN_PROGRESS = 0x9001;
const char* esp_err_to_name(esp_err_t) { return "error"; }

namespace device_info {
size_t free_internal_heap_bytes() { return 96U * 1024U; }
size_t largest_free_internal_block_bytes() { return 48U * 1024U; }
}

struct Grinder { int stops = 0; void stop() { ++stops; } };
struct HardwareManager { Grinder grinder; Grinder* get_grinder() { return &grinder; } };

struct esp_partition_t { size_t size; } inactive{PARTITION_SIZE};
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return &inactive; }

// ---- esp_http_client ----
struct esp_http_client;
using esp_http_client_handle_t = esp_http_client*;
enum esp_http_client_event_id_t { HTTP_EVENT_ON_HEADER = 3, HTTP_EVENT_ON_DATA = 4 };
struct esp_http_client_event_t {
    esp_http_client_event_id_t event_id;
    esp_http_client_handle_t client;
    void* data;
    int data_len;
    void* user_data;
};
struct esp_http_client_config_t {
    const char* url;
    int timeout_ms;
    esp_err_t (*crt_bundle_attach)(void*);
    const char* user_agent;
    int buffer_size;
    int max_redirection_count;
    bool keep_alive_enable;
    esp_err_t (*event_handler)(esp_http_client_event_t*);
    void* user_data;
};
esp_err_t esp_crt_bundle_attach(void*) { return ESP_OK; }

// One HTTP response: its status and the body pieces the server sends.
struct Response { int status; std::vector<std::string> pieces; };
struct esp_http_client {
    esp_http_client_config_t config{};
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<Response> responses;  // redirects first, then the final response
    esp_err_t perform_result = ESP_OK;
    int status = 0;
    int cleanups = 0;
} client;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config) {
    assert(config->crt_bundle_attach && config->url);
    client.config = *config;
    client.headers.clear();
    return &client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t handle, const char* key, const char* value) {
    handle->headers.emplace_back(key, value);
    return ESP_OK;
}
int esp_http_client_get_status_code(esp_http_client_handle_t handle) { return handle->status; }
// Every response's body reaches the event handler, redirects included.
esp_err_t esp_http_client_perform(esp_http_client_handle_t handle) {
    assert(static_cast<int>(handle->responses.size()) <= handle->config.max_redirection_count + 1);
    for (const Response& response : handle->responses) {
        handle->status = response.status;
        for (const std::string& piece : response.pieces) {
            esp_http_client_event_t event{HTTP_EVENT_ON_DATA, handle, const_cast<char*>(piece.data()),
                                          static_cast<int>(piece.size()), handle->config.user_data};
            handle->config.event_handler(&event);
        }
    }
    return handle->perform_result;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t handle) { ++handle->cleanups; return ESP_OK; }

// ---- esp_https_ota ----
struct esp_app_desc_t { char project_name[32]; };
struct esp_https_ota_config_t {
    const esp_http_client_config_t* http_config;
    esp_err_t (*http_client_init_cb)(esp_http_client_handle_t);
};
struct Download {
    // The scenario.
    esp_err_t begin_result = ESP_OK;
    esp_err_t description_result = ESP_OK;
    int image_size = 300000;
    std::vector<int> reads;  // image bytes each perform call adds
    esp_err_t final_perform = ESP_OK;
    bool complete = true;
    esp_err_t finish_result = ESP_OK;
    // What the firmware did.
    esp_http_client_config_t http{};
    std::string url;  // the config's url only lives as long as the download
    bool init_callback_ran = false;
    bool open = false;
    int read = 0;
    size_t performed = 0;
    int finishes = 0, aborts = 0;
} download;
using esp_https_ota_handle_t = Download*;

esp_err_t esp_https_ota_begin(const esp_https_ota_config_t* config, esp_https_ota_handle_t* handle) {
    download.http = *config->http_config;
    download.url = config->http_config->url;
    if (config->http_client_init_cb) {
        esp_http_client_init(config->http_config);
        download.init_callback_ran = config->http_client_init_cb(&client) == ESP_OK;
    }
    if (download.begin_result != ESP_OK) return download.begin_result;
    download.open = true;
    *handle = &download;
    return ESP_OK;
}
esp_err_t esp_https_ota_get_img_desc(esp_https_ota_handle_t handle, esp_app_desc_t* out) {
    assert(handle->open && handle->performed == 0);
    strncpy(out->project_name, "smart-grind-by-weight", sizeof(out->project_name));
    return handle->description_result;
}
int esp_https_ota_get_image_size(esp_https_ota_handle_t handle) { assert(handle->open); return handle->image_size; }
esp_err_t esp_https_ota_perform(esp_https_ota_handle_t handle) {
    assert(handle->open);
    if (handle->performed == handle->reads.size()) return handle->final_perform;
    handle->read += handle->reads[handle->performed++];
    return ESP_ERR_HTTPS_OTA_IN_PROGRESS;
}
int esp_https_ota_get_image_len_read(esp_https_ota_handle_t handle) { assert(handle->open); return handle->read; }
bool esp_https_ota_is_complete_data_received(esp_https_ota_handle_t handle) { assert(handle->open); return handle->complete; }
// finish and abort each release the handle; it may not be used again.
esp_err_t esp_https_ota_finish(esp_https_ota_handle_t handle) {
    assert(handle->open); handle->open = false; ++handle->finishes; return handle->finish_result;
}
esp_err_t esp_https_ota_abort(esp_https_ota_handle_t handle) {
    assert(handle->open); handle->open = false; ++handle->aborts; return ESP_OK;
}

namespace firmware_image {
const char* refusal = nullptr;
const char* check_description(const esp_app_desc_t& description) {
    assert(strcmp(description.project_name, "smart-grind-by-weight") == 0);
    return refusal;
}
}

class DeviceWebServer {
public:
    void perform_github_ota(const std::string& tag);
    void finish_ota(bool success) {
        results.push_back(success);
        // The download handle is released before the result is reported.
        assert(!download.open);
    }
    HardwareManager* hardware_manager_ = nullptr;
    std::atomic<size_t> ota_received_{0};
    std::atomic<size_t> ota_total_{0};
    std::vector<bool> results;
};

@CONSTANTS@
@PRODUCTION@

struct Outcome { bool installed; size_t total, received; };

// Run one download from a fresh scenario.
Outcome run(const Download& scenario, const char* refusal = nullptr) {
    download = scenario;
    firmware_image::refusal = refusal;
    HardwareManager hardware;
    DeviceWebServer server;
    server.hardware_manager_ = &hardware;
    server.perform_github_ota("v1.6.0");
    assert(server.results.size() == 1 && hardware.grinder.stops == 1);
    assert(!download.open);
    return {server.results[0], server.ota_total_.load(), server.ota_received_.load()};
}

int main() {
    // A good release is downloaded from the mirror, reported, finished once.
    Download good;
    good.reads = {100000, 150000, 50000};
    Outcome outcome = run(good);
    assert(outcome.installed && download.finishes == 1 && download.aborts == 0);
    assert(outcome.total == 300000 && outcome.received == 300000);
    assert(download.url == std::string(GITHUB_RELEASE_MIRROR_BASE) + "v1.6.0/smart-grind-by-weight-v1.6.0" +
                      HW_RELEASE_FIRMWARE_SUFFIX + ".bin");
    assert(download.http.crt_bundle_attach && download.http.timeout_ms == OTA_READ_TIMEOUT_MS);
    assert(download.http.max_redirection_count == HTTP_MAX_REDIRECTS);
    assert(download.http.buffer_size == OTA_DOWNLOAD_BUFFER && download.http.keep_alive_enable);
    assert(download.init_callback_ran && client.headers.size() == 1 &&
           client.headers[0].first == "Cache-Control" && client.headers[0].second == "no-cache");

    // An image refused by its description, one whose description cannot be
    // read, and one of no or excessive size are abandoned before any data is
    // written, which is when esp_https_ota_perform() would erase flash.
    for (int image_size : {0, -1, static_cast<int>(PARTITION_SIZE) + 1}) {
        Download sized = good; sized.image_size = image_size;
        assert(!run(sized).installed && download.performed == 0 && download.aborts == 1);
    }
    assert(!run(good, "Not Smart Grind firmware").installed && download.performed == 0);
    Download unreadable = good; unreadable.description_result = ESP_FAIL;
    assert(!run(unreadable).installed && download.performed == 0 && download.aborts == 1);

    // A download that fails part-way, or ends without the whole image, is
    // aborted and never finished.
    Download dropped = good; dropped.final_perform = ESP_FAIL; dropped.reads = {100000};
    outcome = run(dropped);
    assert(!outcome.installed && download.aborts == 1 && download.finishes == 0);
    assert(outcome.received == 100000);
    Download short_body = good; short_body.complete = false;
    assert(!run(short_body).installed && download.aborts == 1 && download.finishes == 0);

    // An image that fails validation in finish is reported as a failure.
    Download invalid = good; invalid.finish_result = ESP_FAIL;
    assert(!run(invalid).installed && download.finishes == 1 && download.aborts == 0);

    // A download that cannot start has nothing to release.
    Download unreachable = good; unreachable.begin_result = ESP_FAIL;
    assert(!run(unreachable).installed && download.finishes == 0 && download.aborts == 0);

    // The manifest fetch keeps only the final response's body, after redirects.
    std::string body;
    client.responses = {{302, {"<html>moved</html>"}}, {200, {"{\"tag\":", "\"v1.6.0\"}"}}};
    client.perform_result = ESP_OK; client.cleanups = 0;
    assert(fetch_text("https://example/latest.json", "agent", 10000, 512, body));
    assert(body == "{\"tag\":\"v1.6.0\"}" && client.cleanups == 1);
    assert(client.config.max_redirection_count == HTTP_MAX_REDIRECTS && client.config.crt_bundle_attach);
    assert(client.headers.size() == 1 && client.headers[0].second == "no-cache");
    // An oversized, empty, failed or unsuccessful response leaves `body` alone.
    const std::string kept = body;
    client.responses = {{200, {std::string(400, 'x'), std::string(200, 'y')}}};
    assert(!fetch_text("https://example/latest.json", "agent", 10000, 512, body) && body == kept);
    client.responses = {{200, {}}};
    assert(!fetch_text("https://example/latest.json", "agent", 10000, 512, body) && body == kept);
    client.responses = {{404, {"missing"}}};
    assert(!fetch_text("https://example/latest.json", "agent", 10000, 512, body) && body == kept);
    client.responses = {{200, {"{}"}}}; client.perform_result = ESP_FAIL;
    assert(!fetch_text("https://example/latest.json", "agent", 10000, 512, body) && body == kept);
}
'''


class OtaStreamTest(unittest.TestCase):
    def test_release_download_and_manifest_fetch(self):
        constants = "\n".join(constants_used_by(SOURCE, PRODUCTION))
        harness = HARNESS.replace("@CONSTANTS@", constants).replace("@PRODUCTION@", PRODUCTION)
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / "download.cpp", Path(folder) / "download"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            f"-DPARTITION_SIZE={PARTITION_SIZE}U", "-I", str(ROOT / "src"),
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10, stderr=subprocess.DEVNULL)


if __name__ == "__main__":
    unittest.main()
