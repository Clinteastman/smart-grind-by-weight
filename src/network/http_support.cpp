#include "http_support.h"

#include <esp_err.h>
#include <sdkconfig.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

#include "../config/constants.h"
#include "../storage/filesystem.h"
#include "../system/timing.h"

// websocket_route hands the connection-time call to the server's post-handshake
// callback. Without it a WebSocket client is never admitted or origin-checked.
#if !CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT
#error "WebSocket routes need CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT=y (ESP-IDF 5.5.5 or later)"
#endif

namespace http {
namespace {

// Handlers are registered once at start-up and live for the lifetime of the
// process. A deque keeps their addresses stable as more are added.
std::deque<Handler>& handler_store() {
    static std::deque<Handler> handlers;
    return handlers;
}

Handler& not_found_handler() {
    static Handler handler;
    return handler;
}

esp_err_t dispatch(httpd_req_t* request) {
    auto* handler = static_cast<Handler*>(request->user_ctx);
    return handler ? (*handler)(request) : ESP_FAIL;
}

const char* status_line(int status) {
    switch (status) {
        case 200: return "200 OK";
        case 202: return "202 Accepted";
        case 302: return "302 Found";
        case 400: return "400 Bad Request";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 409: return "409 Conflict";
        case 413: return "413 Payload Too Large";
        case 500: return "500 Internal Server Error";
        case 503: return "503 Service Unavailable";
        default: return "200 OK";
    }
}

/** Locate `key` in a `&`-separated key=value list, returning its raw value. */
bool find_pair(const std::string& pairs, const char* key, std::string& value,
               bool& present) {
    const size_t key_length = strlen(key);
    size_t position = 0;
    while (position <= pairs.size()) {
        const size_t end = pairs.find('&', position);
        const size_t stop = end == std::string::npos ? pairs.size() : end;
        const size_t equals = pairs.find('=', position);
        const size_t name_end = (equals == std::string::npos || equals > stop) ? stop : equals;
        // Compare in place: a settings POST looks up ~60 fields in one body.
        if (name_end - position == key_length &&
            pairs.compare(position, key_length, key) == 0) {
            present = true;
            value = name_end == stop ? std::string()
                                     : pairs.substr(name_end + 1, stop - name_end - 1);
            return true;
        }
        if (end == std::string::npos) break;
        position = end + 1;
    }
    present = false;
    return false;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** Decode application/x-www-form-urlencoded escaping, including '+' as space. */
std::string url_decode(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            decoded += ' ';
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const int high = hex_value(value[i + 1]);
            const int low = hex_value(value[i + 2]);
            if (high < 0 || low < 0) {
                decoded += value[i];
            } else {
                decoded += static_cast<char>((high << 4) | low);
                i += 2;
            }
        } else {
            decoded += value[i];
        }
    }
    return decoded;
}

}  // namespace

namespace {

bool register_route(httpd_handle_t server, const char* uri, httpd_method_t method,
                    Handler handler, bool websocket) {
    if (!server || !uri) return false;

    handler_store().push_back(std::move(handler));
    httpd_uri_t descriptor = {};
    descriptor.uri = uri;
    descriptor.method = method;
    descriptor.handler = dispatch;
    descriptor.user_ctx = &handler_store().back();
    descriptor.is_websocket = websocket;
    // Control frames reach the handler so a client close can drop its slot.
    descriptor.handle_ws_control_frames = websocket;
    // The server calls the handler only for frames; the handshake GET arrives
    // through this callback instead.
    if (websocket) descriptor.ws_post_handshake_cb = dispatch;

    const esp_err_t err = httpd_register_uri_handler(server, &descriptor);
    if (err != ESP_OK) {
        LOG_BLE("[WEB] Could not register %s%s: %s\n", websocket ? "WebSocket " : "", uri,
                esp_err_to_name(err));
        handler_store().pop_back();
        return false;
    }
    return true;
}

}  // namespace

bool route(httpd_handle_t server, const char* uri, httpd_method_t method, Handler handler) {
    return register_route(server, uri, method, std::move(handler), false);
}

bool websocket_route(httpd_handle_t server, const char* uri, Handler handler) {
    return register_route(server, uri, HTTP_GET, std::move(handler), true);
}

bool not_found(httpd_handle_t server, Handler handler) {
    if (!server) return false;
    // httpd_register_err_handler passes the error code instead of a context
    // pointer, so the single fallback handler lives in a file-scope slot.
    not_found_handler() = std::move(handler);
    return httpd_register_err_handler(
               server, HTTPD_404_NOT_FOUND,
               [](httpd_req_t* request, httpd_err_code_t) -> esp_err_t {
                   Handler& handler = not_found_handler();
                   return handler ? handler(request) : ESP_FAIL;
               }) == ESP_OK;
}

esp_err_t send(httpd_req_t* request, int status, const char* content_type,
               const std::string& body) {
    httpd_resp_set_status(request, status_line(status));
    httpd_resp_set_type(request, content_type);
    return httpd_resp_send(request, body.data(), body.size());
}

esp_err_t send_json(httpd_req_t* request, int status, const std::string& body) {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return send(request, status, "application/json", body);
}

esp_err_t send_error(httpd_req_t* request, int status, const char* message) {
    return send_json(request, status,
                     std::string("{\"error\":\"") + json_escape(message) + "\"}");
}

esp_err_t send_redirect(httpd_req_t* request, const char* location) {
    httpd_resp_set_status(request, status_line(302));
    httpd_resp_set_hdr(request, "Location", location);
    return httpd_resp_send(request, "Open Smart Grind setup", HTTPD_RESP_USE_STRLEN);
}

esp_err_t send_file(httpd_req_t* request, const char* path, const char* content_type,
                    bool download) {
    FsFile file = filesystem.open(path, "r");
    if (!file) return send_error(request, 404, "File not found");

    httpd_resp_set_type(request, content_type);
    httpd_resp_set_hdr(request, "Cache-Control", "private, no-store");
    if (download) {
        httpd_resp_set_hdr(request, "Content-Disposition", "attachment");
    }

    // 1 KB chunks keep the peak allocation small while an OTA or a grind may be
    // competing for internal RAM.
    std::vector<uint8_t> buffer(1024);
    while (true) {
        const size_t read = file.read(buffer.data(), buffer.size());
        if (read == 0) break;
        if (httpd_resp_send_chunk(request, reinterpret_cast<const char*>(buffer.data()),
                                  read) != ESP_OK) {
            return ESP_FAIL;
        }
    }
    return httpd_resp_send_chunk(request, nullptr, 0);
}

bool stream_body(httpd_req_t* request, const BodySink& sink) {
    // 2 KB keeps the peak allocation small; uploads run while Bluetooth has been
    // torn down to free internal RAM.
    std::vector<uint8_t> chunk(2048);
    size_t remaining = request->content_len;
    uint32_t idle_deadline = millis() + BODY_IDLE_TIMEOUT_MS;
    while (remaining > 0) {
        const int received =
            httpd_req_recv(request, reinterpret_cast<char*>(chunk.data()),
                           remaining < chunk.size() ? remaining : chunk.size());
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            // A socket timeout alone is not fatal, but retrying forever would
            // strand this task and everything waiting on the transfer.
            if (static_cast<int32_t>(millis() - idle_deadline) >= 0) {
                LOG_BLE("[WEB] Abandoning stalled request body after %lu bytes\n",
                        static_cast<unsigned long>(request->content_len - remaining));
                return false;
            }
            continue;
        }
        if (received <= 0) return false;
        idle_deadline = millis() + BODY_IDLE_TIMEOUT_MS;
        remaining -= static_cast<size_t>(received);
        if (!sink(chunk.data(), static_cast<size_t>(received))) return false;
    }
    return true;
}

bool read_body(httpd_req_t* request, std::string& body, size_t max_bytes) {
    if (request->content_len > max_bytes) return false;

    body.clear();
    body.reserve(request->content_len);
    return stream_body(request, [&body](const uint8_t* data, size_t length) {
        body.append(reinterpret_cast<const char*>(data), length);
        return true;
    });
}

bool query_param(httpd_req_t* request, const char* key, std::string& value) {
    const size_t length = httpd_req_get_url_query_len(request);
    if (length == 0) return false;

    std::string query(length + 1, '\0');
    if (httpd_req_get_url_query_str(request, query.data(), query.size()) != ESP_OK) {
        return false;
    }
    query.resize(length);

    bool present = false;
    std::string raw;
    if (!find_pair(query, key, raw, present) || !present) return false;
    value = url_decode(raw);
    return true;
}

bool has_query_param(httpd_req_t* request, const char* key) {
    std::string ignored;
    return query_param(request, key, ignored);
}

bool form_field(const std::string& body, const char* key, std::string& value) {
    bool present = false;
    std::string raw;
    if (!find_pair(body, key, raw, present) || !present) return false;
    value = url_decode(raw);
    return true;
}

bool has_form_field(const std::string& body, const char* key) {
    std::string ignored;
    return form_field(body, key, ignored);
}

std::string header(httpd_req_t* request, const char* name) {
    const size_t length = httpd_req_get_hdr_value_len(request, name);
    if (length == 0) return {};

    std::string value(length + 1, '\0');
    if (httpd_req_get_hdr_value_str(request, name, value.data(), value.size()) != ESP_OK) {
        return {};
    }
    value.resize(length);
    return value;
}

bool origin_allowed(httpd_req_t* request) {
    const std::string origin = header(request, "Origin");
    if (origin.empty()) return true;
    return origin == "http://" + header(request, "Host");
}

Handler same_origin(Handler handler) {
    return [handler = std::move(handler)](httpd_req_t* request) {
        if (!origin_allowed(request)) {
            return send_error(request, 403, "Request origin is not allowed");
        }
        return handler(request);
    };
}

std::string json_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (static_cast<uint8_t>(ch) >= 0x20) escaped += ch;
                break;
        }
    }
    return escaped;
}

}  // namespace http
