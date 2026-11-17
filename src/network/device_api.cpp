#include "device_api.h"
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <cstdint>
#include <cstdio>
#include <esp_system.h>
#include "../system/device_info.h"
#include "../system/timing.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include "../storage/filesystem.h"
#include <esp_random.h>

#include "../controllers/grind_controller.h"
#include "../controllers/profile_controller.h"
#include "../config/constants.h"
#include "../hardware/WeightSensor.h"
#include "../hardware/grinder.h"
#include "../hardware/hardware_manager.h"
#include "../system/screensaver_settings.h"
#include "../system/operation_interlock.h"
#include "device_web_server.h"
#include "gaggimate_status_client.h"
#include <lwip/sockets.h>

#include "http_support.h"
#include "../system/string_utils.h"

DeviceApi device_api;

namespace {
bool contains_json_string(const char* json, const char* key, const char* value) {
    if (!json || !key || !value) return false;
    const std::string needle = "\"" + std::string(key) + "\"";
    const char* cursor = strstr(json, needle.c_str());
    if (!cursor) return false;
    cursor += needle.length();
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
    if (*cursor++ != ':') return false;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
    if (*cursor++ != '\"') return false;
    const size_t value_length = strlen(value);
    return strncmp(cursor, value, value_length) == 0 && cursor[value_length] == '\"';
}

bool extract_json_uint(const char* json, const char* key, uint32_t& value) {
    if (!json || !key) return false;
    const std::string needle = "\"" + std::string(key) + "\"";
    const char* cursor = strstr(json, needle.c_str());
    if (!cursor) return false;
    cursor += needle.length();
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
    if (*cursor++ != ':') return false;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
    if (*cursor < '0' || *cursor > '9') return false;

    uint64_t parsed = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        parsed = parsed * 10U + static_cast<uint8_t>(*cursor - '0');
        if (parsed > 0xFFFFFFFFULL) return false;
        ++cursor;
    }
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
    if (*cursor != ',' && *cursor != '}') return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

const char* api_phase_name(const GrindController& controller) {
    switch (controller.get_phase()) {
        case GrindPhase::IDLE:
            return "IDLE";
        case GrindPhase::INITIALIZING:
        case GrindPhase::SETUP:
        case GrindPhase::TARING:
        case GrindPhase::TARE_CONFIRM:
            return "PREPARING";
        case GrindPhase::PRIME:
        case GrindPhase::PRIME_SETTLING:
        case GrindPhase::PURGE_CONFIRM:
            return "PRIMING";
        case GrindPhase::PREDICTIVE:
        case GrindPhase::PULSE_EXECUTE:
        case GrindPhase::TIME_ADDITIONAL_PULSE:
            return "GRINDING";
        case GrindPhase::TIME_GRINDING:
            return controller.is_grind_paused() ? "PAUSED" : "GRINDING";
        case GrindPhase::MANUAL_GRINDING:
            return "GRINDING";
        case GrindPhase::PULSE_DECISION:
        case GrindPhase::PULSE_SETTLING:
            return "COASTING";
        case GrindPhase::FINAL_SETTLING:
            return "FINAL_SETTLING";
        case GrindPhase::COMPLETED:
            return "COMPLETED";
        case GrindPhase::TIMEOUT:
            return "TIMEOUT";
    }
    return "IDLE";
}
}

void DeviceApi::attach_routes(httpd_handle_t server, HardwareManager* hardware,
                              GrindController* grind_controller,
                              ProfileController* profile_controller) {
    if (initialized_ || !server || !hardware || !grind_controller || !profile_controller) return;
    server_ = server;
    hardware_ = hardware;
    grind_controller_ = grind_controller;
    profile_controller_ = profile_controller;
    command_queue_ = xQueueCreate(8, sizeof(Command));
    settings_mutex_ = xSemaphoreCreateMutex();
    ws_send_mutex_ = xSemaphoreCreateMutex();
    if (!command_queue_ || !settings_mutex_ || !ws_send_mutex_) {
        hardware_ = nullptr;
        grind_controller_ = nullptr;
        profile_controller_ = nullptr;
        return;
    }
    for (auto& slot : client_fds_) slot.store(NO_CLIENT);

    http::websocket_route(server_, "/ws", [this](httpd_req_t* request) {
        return handle_websocket(request);
    });
    configure_settings_routes();
    // Avoid mistaking a previous boot's result for a newly queued request.
    next_settings_id_ = esp_random();
    refresh_settings_cache();
    initialized_ = true;
}

bool form_bool(const std::string& value) {
    return value == "1" || value == "true" || value == "on";
}

void DeviceApi::update() {
    if (!initialized_) return;
    if (settings_cache_dirty_.exchange(false)) refresh_settings_cache();
    const uint32_t now = millis();
    if (now - last_publish_ms_ < PUBLISH_INTERVAL_MS) return;
    last_publish_ms_ = now;

    // Nothing to publish to, and building the frame costs several double
    // conversions plus a heap walk. Zero clients is the steady state.
    bool any_client = false;
    for (const auto& slot : client_fds_) {
        if (slot.load() != NO_CLIENT) {
            any_client = true;
            break;
        }
    }
    if (!any_client) return;

    const std::string message = build_state_message();
    for (size_t index = 0; index < MAX_CLIENTS; ++index) {
        auto& slot = client_fds_[index];
        const int fd = slot.load();
        if (fd == NO_CLIENT) {
            backpressure_skips_[index].store(0);
            continue;
        }
        // A session the server closed without a CLOSE frame, by LRU purge or a
        // dropped peer, never reaches handle_websocket, and lwIP hands its
        // descriptor to the next connection. Forget it rather than writing
        // frames into an unrelated HTTP response.
        if (httpd_ws_get_fd_info(server_, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            slot.store(NO_CLIENT);
            backpressure_skips_[index].store(0);
            continue;
        }
        if (send_text(fd, message)) {
            backpressure_skips_[index].store(0);
            continue;
        }
        // A send only fails when the socket buffer is full or the peer is gone.
        // Give a slow client a bounded number of skipped frames before closing.
        uint8_t skipped = backpressure_skips_[index].load();
        if (skipped < MAX_CONSECUTIVE_BACKPRESSURE_SKIPS) ++skipped;
        backpressure_skips_[index].store(skipped);
        if (skipped >= MAX_CONSECUTIVE_BACKPRESSURE_SKIPS) {
            LOG_BLE("[WEB] Closing WebSocket client %d after sustained backpressure\n", fd);
            httpd_sess_trigger_close(server_, fd);
            slot.store(NO_CLIENT);
            backpressure_skips_[index].store(0);
        }
    }
}

bool DeviceApi::send_text(int client_fd, const std::string& message) {
    if (!server_ || client_fd == NO_CLIENT || !ws_send_mutex_) return false;

    // httpd_ws_send_frame_async calls send() on the caller's task, so a peer
    // that has stopped reading would block us for the full socket send timeout.
    // Publishing runs on the service loop and acknowledgements on the UI task,
    // so that stall would freeze the display. Skip instead, and let the
    // backpressure counter evict the client.
    if (!socket_writable(client_fd)) return false;
    // Bounded, for the same reason: never queue behind another task's send.
    if (xSemaphoreTake(ws_send_mutex_, pdMS_TO_TICKS(WS_SEND_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(message.data()));
    frame.len = message.size();
    const esp_err_t err = httpd_ws_send_frame_async(server_, client_fd, &frame);
    xSemaphoreGive(ws_send_mutex_);
    return err == ESP_OK;
}

bool DeviceApi::socket_writable(int client_fd) {
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(client_fd, &writable);
    timeval immediately = {};
    return select(client_fd + 1, nullptr, &writable, nullptr, &immediately) > 0;
}

bool DeviceApi::process_commands() {
    if (!initialized_) return false;
    if (applying_settings_id_) return true;
    bool settings_changed = false;
    Command command{};
    while (xQueueReceive(command_queue_, &command, 0) == pdTRUE) {
        // Keep the idle check and its action together with controller updates.
        // The acknowledgement is sent after unlocking: a WebSocket send can
        // block on a slow client, and the grind control loop needs this lock.
        auto control_lock = grind_controller_->lock_control();
        struct {
            const char* action = nullptr;
            bool accepted = false;
            const char* reason = nullptr;
        } ack;
        switch (command.action) {
            case CommandAction::START: {
                if (device_web_server.is_ota_active() || device_web_server.is_ota_preparing()) {
                    ack = {"start", false, "firmware update is active"};
                    break;
                }
                if (grind_controller_->get_phase() != GrindPhase::IDLE) {
                    ack = {"start", false, "grinder is not idle"};
                    break;
                }
                const auto profile = profile_controller_->snapshot();
                const GrindMode mode = profile.mode;
                WeightSensor* sensor = hardware_->get_weight_sensor();
                if (mode == GrindMode::WEIGHT &&
                    (!sensor || sensor->has_hardware_fault())) {
                    ack = {"start", false, "load cell is not ready"};
                    break;
                }
                grind_controller_->set_grind_profile_id(profile.current_profile);
                const float target_weight = profile.profiles[profile.current_profile].weight;
                const uint32_t target_time_ms = static_cast<uint32_t>(
                    profile.profiles[profile.current_profile].time_seconds * 1000.0f + 0.5f);
                const bool started = grind_controller_->start_grind(target_weight, target_time_ms, mode);
                ack = {"start", started, started ? "grind started" : "grind could not start"};
                break;
            }
            case CommandAction::START_MANUAL:
                if (device_web_server.is_ota_active() || device_web_server.is_ota_preparing()) {
                    ack = {"start_manual", false, "firmware update is active"};
                } else if (grind_controller_->get_phase() != GrindPhase::IDLE) {
                    ack = {"start_manual", false, "grinder is not idle"};
                } else {
                    const bool started = grind_controller_->start_grind(0.0f, 0, GrindMode::MANUAL);
                    ack = {"start_manual", started,
                           started ? "manual grind started" : "grind could not start"};
                }
                break;
            case CommandAction::STOP:
                if (!grind_controller_->is_active()) {
                    ack = {"stop", false, "grinder is not active"};
                } else {
                    grind_controller_->stop_grind();
                    ack = {"stop", true, "grind stopped"};
                }
                break;
            case CommandAction::DISMISS:
                if (grind_controller_->get_phase() != GrindPhase::COMPLETED &&
                    grind_controller_->get_phase() != GrindPhase::TIMEOUT) {
                    ack = {"dismiss", false, "nothing to dismiss"};
                } else {
                    grind_controller_->return_to_idle();
                    const bool dismissed = grind_controller_->get_phase() == GrindPhase::IDLE;
                    ack = {"dismiss", dismissed,
                           dismissed ? "result dismissed" : "history completion is pending"};
                }
                break;
            case CommandAction::TARE: {
                WeightSensor* sensor = hardware_->get_weight_sensor();
                if (grind_controller_->get_phase() != GrindPhase::IDLE) {
                    ack = {"tare", false, "grinder is not idle"};
                } else if (!sensor || sensor->has_hardware_fault() || sensor->get_sample_count() <= 0) {
                    ack = {"tare", false, "load cell is not ready"};
                } else if (sensor->is_tare_in_progress()) {
                    ack = {"tare", false, "tare is already in progress"};
                } else {
                    sensor->tareNoDelay();
                    ack = {"tare", true, "tare started"};
                }
                break;
            }
            case CommandAction::SELECT_PROFILE:
                if (grind_controller_->get_phase() != GrindPhase::IDLE) {
                    ack = {"select_profile", false, "grinder is not idle"};
                    break;
                }
                profile_controller_->set_current_profile(command.profile_index);
                settings_changed = true;
                LOG_BLE("[WEB] Active profile changed to %s\n",
                        profile_controller_->get_current_name());
                ack = {"select_profile", true, "profile selected"};
                break;
            case CommandAction::SET_MODE:
                if (grind_controller_->get_phase() != GrindPhase::IDLE) {
                    ack = {"set_mode", false, "grinder is not idle"};
                    break;
                }
                profile_controller_->set_grind_mode(
                    command.grind_mode == 1 ? GrindMode::TIME : GrindMode::WEIGHT);
                settings_changed = true;
                ack = {"set_mode", true, "grind mode selected"};
                break;
            case CommandAction::APPLY_SETTINGS: {
                settings_operation_token_ = operation_interlock().try_acquire();
                if (!settings_operation_token_) {
                    set_settings_result(command.request_id, "busy");
                    break;
                }
                applying_settings_id_ = command.request_id;
                settings_persisted_ = apply_settings(command.settings);
                // Even a failed multi-key save can change stored values. Return
                // now so UI runtime refresh finishes before any later command.
                refresh_settings_cache();
                return true;
            }
        }
        control_lock.unlock();
        if (ack.action) send_ack(command, ack.action, ack.accepted, ack.reason);
    }
    if (settings_changed) refresh_settings_cache();
    return settings_changed;
}

uint32_t DeviceApi::reserve_settings_result() {
    xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    for (const auto& result : settings_results_) {
        if (strcmp(result.status, "pending") == 0) {
            xSemaphoreGive(settings_mutex_);
            return 0;
        }
    }
    if (++next_settings_id_ == 0) ++next_settings_id_;
    const uint32_t id = next_settings_id_;
    settings_results_[next_settings_result_] = {id, "pending"};
    next_settings_result_ = (next_settings_result_ + 1) % 4;
    xSemaphoreGive(settings_mutex_);
    return id;
}

void DeviceApi::set_settings_result(uint32_t id, const char* status) {
    xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    for (auto& result : settings_results_) {
        if (result.id == id) { result.status = status; break; }
    }
    xSemaphoreGive(settings_mutex_);
}

const char* DeviceApi::settings_result(uint32_t id) {
    const char* status = "unknown";
    xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    for (const auto& result : settings_results_) {
        if (id && result.id == id) { status = result.status; break; }
    }
    xSemaphoreGive(settings_mutex_);
    return status; // All statuses are static literals, not pointers into a slot.
}

void DeviceApi::complete_settings_application(bool runtime_applied) {
    if (!applying_settings_id_) return;
    // Keep other operations excluded through persistence, cache and UI refresh.
    set_settings_result(applying_settings_id_, settings_persisted_ && runtime_applied ? "saved" : "failed");
    operation_interlock().release(settings_operation_token_);
    settings_operation_token_ = 0;
    applying_settings_id_ = 0;
}

esp_err_t DeviceApi::handle_websocket(httpd_req_t* request) {
    const int client_fd = httpd_req_to_sockfd(request);

    // A GET reaches the handler once, straight after a successful handshake.
    if (request->method == HTTP_GET) {
        if (!http::origin_allowed(request)) {
            httpd_sess_trigger_close(server_, client_fd);
            return ESP_OK;
        }
        add_client(client_fd);
        return ESP_OK;
    }

    // Frames are accepted only from a client admitted above, whose Origin
    // matched; any other socket could start the motor from a foreign page.
    if (!is_client(client_fd)) {
        httpd_sess_trigger_close(server_, client_fd);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {};
    if (httpd_ws_recv_frame(request, &frame, 0) != ESP_OK) {
        remove_client(client_fd);
        return ESP_FAIL;
    }

    // This route asks for control frames so a client close releases its slot,
    // which also makes consuming and answering them our responsibility: a
    // payload left unread would be parsed as the next frame's header.
    if (frame.type == HTTPD_WS_TYPE_CLOSE || frame.type == HTTPD_WS_TYPE_PING ||
        frame.type == HTTPD_WS_TYPE_PONG) {
        uint8_t control_payload[125] = {};
        frame.payload = control_payload;
        if (httpd_ws_recv_frame(request, &frame, sizeof(control_payload)) != ESP_OK) {
            remove_client(client_fd);
            return ESP_FAIL;
        }
        if (frame.type == HTTPD_WS_TYPE_PONG) return ESP_OK;
        const bool closing = frame.type == HTTPD_WS_TYPE_CLOSE;
        if (closing) {
            remove_client(client_fd);
            // RFC 6455 section 5.5.1: echo an empty CLOSE.
            frame.type = HTTPD_WS_TYPE_CLOSE;
            frame.len = 0;
            frame.payload = nullptr;
        } else {
            // RFC 6455 section 5.5.2: a PONG carries the PING's payload back.
            frame.type = HTTPD_WS_TYPE_PONG;
        }
        if (xSemaphoreTake(ws_send_mutex_, pdMS_TO_TICKS(WS_SEND_MUTEX_TIMEOUT_MS)) != pdTRUE) {
            return ESP_FAIL;
        }
        const esp_err_t err = httpd_ws_send_frame(request, &frame);
        xSemaphoreGive(ws_send_mutex_);
        return err;
    }

    if (frame.type != HTTPD_WS_TYPE_TEXT || frame.len == 0 || frame.len > 255) {
        httpd_sess_trigger_close(server_, client_fd);
        remove_client(client_fd);
        return ESP_OK;
    }

    std::vector<uint8_t> payload(frame.len + 1, 0);
    frame.payload = payload.data();
    if (httpd_ws_recv_frame(request, &frame, frame.len) != ESP_OK) {
        remove_client(client_fd);
        return ESP_FAIL;
    }
    queue_command(client_fd, payload.data(), frame.len);
    return ESP_OK;
}

void DeviceApi::add_client(int client_fd) {
    // A reused descriptor may still hold a slot from the session it replaced.
    remove_client(client_fd);
    for (size_t index = 0; index < MAX_CLIENTS; ++index) {
        auto& slot = client_fds_[index];
        int empty = NO_CLIENT;
        if (slot.compare_exchange_strong(empty, client_fd)) {
            backpressure_skips_[index].store(0);
            send_text(client_fd, build_state_message());
            return;
        }
    }
    LOG_BLE("[WEB] Refusing WebSocket client %d: too many clients\n", client_fd);
    httpd_sess_trigger_close(server_, client_fd);
}

void DeviceApi::remove_client(int client_fd) {
    for (size_t index = 0; index < MAX_CLIENTS; ++index) {
        auto& slot = client_fds_[index];
        int expected = client_fd;
        if (slot.compare_exchange_strong(expected, NO_CLIENT)) {
            backpressure_skips_[index].store(0);
        }
    }
}

bool DeviceApi::is_client(int client_fd) const {
    if (client_fd == NO_CLIENT) return false;
    for (const auto& slot : client_fds_) {
        if (slot.load() == client_fd) return true;
    }
    return false;
}

void DeviceApi::queue_command(int client_fd, const uint8_t* data, size_t len) {
    char json[256];
    memcpy(json, data, len);
    json[len] = '\0';
    uint32_t request_id = 0;
    const bool has_request_id = extract_json_uint(json, "rid", request_id);
    if (!contains_json_string(json, "type", "command")) {
        send_ack(client_fd, request_id, has_request_id, "unknown", false, "invalid message type");
        return;
    }

    Command command{};
    command.client_fd = client_fd;
    command.request_id = request_id;
    command.has_request_id = has_request_id;
    command.action = CommandAction::STOP;
    const char* action = nullptr;
    if (contains_json_string(json, "action", "start")) {
        action = "start";
        command.action = CommandAction::START;
    } else if (contains_json_string(json, "action", "start_manual")) {
        action = "start_manual";
        command.action = CommandAction::START_MANUAL;
    } else if (contains_json_string(json, "action", "stop")) {
        action = "stop";
        command.action = CommandAction::STOP;
    } else if (contains_json_string(json, "action", "dismiss")) {
        action = "dismiss";
        command.action = CommandAction::DISMISS;
    } else if (contains_json_string(json, "action", "tare")) {
        action = "tare";
        command.action = CommandAction::TARE;
    } else if (contains_json_string(json, "action", "select_profile")) {
        action = "select_profile";
        command.action = CommandAction::SELECT_PROFILE;
        uint32_t profile = 0;
        if (!extract_json_uint(json, "profile", profile) || profile >= USER_PROFILE_COUNT) {
            send_ack(command, action, false, "invalid profile");
            return;
        }
        command.profile_index = static_cast<int>(profile);
    } else if (contains_json_string(json, "action", "set_mode")) {
        action = "set_mode";
        command.action = CommandAction::SET_MODE;
        if (contains_json_string(json, "mode", "weight")) {
            command.grind_mode = 0;
        } else if (contains_json_string(json, "mode", "time")) {
            command.grind_mode = 1;
        } else {
            send_ack(command, action, false, "invalid grind mode");
            return;
        }
    } else {
        send_ack(client_fd, request_id, has_request_id, "unknown", false, "unsupported action");
        return;
    }

    if (xQueueSend(command_queue_, &command, 0) != pdTRUE) {
        send_ack(command, action, false, "command queue busy");
    }
}

void DeviceApi::configure_settings_routes() {
    http::route(server_, "/api/v1/settings/result", HTTP_GET, [this](httpd_req_t* request) {
        uint32_t id = 0;
        std::string value;
        bool valid = http::query_param(request, "id", value);
        if (valid) {
            valid = !value.empty() && value.length() <= 10;
            uint64_t parsed = 0;
            for (size_t i = 0; valid && i < value.length(); ++i) {
                valid = value[i] >= '0' && value[i] <= '9';
                if (valid) parsed = parsed * 10 + (value[i] - '0');
            }
            valid = valid && parsed > 0 && parsed <= UINT32_MAX;
            if (valid) id = static_cast<uint32_t>(parsed);
        }
        if (!valid) return http::send_error(request, 400, "Invalid settings request ID");
        const char* status = settings_result(id);
        char body[96];
        snprintf(body, sizeof(body), "{\"request_id\":%lu,\"status\":\"%s\"}",
                 static_cast<unsigned long>(id), status);
        return http::send_json(request, strcmp(status, "unknown") == 0 ? 404 : 200, body);
    });
    http::route(server_, "/api/v1/profile", HTTP_POST, http::same_origin([this](httpd_req_t* request) {
        return queue_profile_selection(request);
    }));
    http::route(server_, "/api/v1/settings", HTTP_GET, [this](httpd_req_t* request) {
        return http::send_json(request, 200, settings_json());
    });
    http::route(server_, "/api/v1/settings", HTTP_POST, http::same_origin([this](httpd_req_t* request) {
        return queue_settings_update(request);
    }));
}

esp_err_t DeviceApi::queue_profile_selection(httpd_req_t* request) {
    std::string form;
    std::string value;
    if (!http::read_body(request, form, 256) || !http::form_field(form, "profile", value)) {
        return http::send_error(request, 400, "Missing profile");
    }
    if (value != "0" && value != "1" && value != "2") {
        return http::send_error(request, 400, "Profile must be 0, 1 or 2");
    }
    if (grind_controller_->get_phase() != GrindPhase::IDLE) {
        return http::send_error(request, 409,
                                "Profiles can only be changed while the grinder is idle");
    }

    Command command{};
    command.client_fd = NO_CLIENT;
    command.action = CommandAction::SELECT_PROFILE;
    command.profile_index = strings::to_int32(value);
    if (xQueueSend(command_queue_, &command, 0) != pdTRUE) {
        return http::send_error(request, 503, "Command queue is busy; try again");
    }
    return http::send_json(request, 202, "{\"accepted\":true}");
}

esp_err_t DeviceApi::queue_settings_update(httpd_req_t* request) {
    static const char* required[] = {
        "current_profile", "grind_mode", "weight0", "weight1", "weight2",
        "time0", "time1", "time2", "auto_start", "auto_return", "purge_mode",
        "purge_amount_g", "freshness_hours", "coast_ratio", "logging_enabled",
        "swipe_enabled", "brightness_percent", "screensaver_brightness_percent",
        "screensaver_startup", "screensaver_sleep", "screensaver_idle_timeout_s",
        "screensaver_startup_timeout_s",
        "screensaver_style", "bluetooth_startup"
    };
    std::string form;
    if (!http::read_body(request, form, 2048)) {
        return http::send_error(request, 400, "Could not read the submitted settings");
    }
    for (const char* field : required) {
        if (!http::has_form_field(form, field)) {
            return http::send_error(request, 400,
                                    (std::string("Missing field: ") + field).c_str());
        }
    }

    auto has = [&form](const char* name) { return http::has_form_field(form, name); };
    auto value = [&form](const char* name) {
        std::string result;
        http::form_field(form, name, result);
        return result;
    };
    DeviceSettingsUpdate settings{};
    settings.current_profile = strings::to_int32(value("current_profile"));
    settings.grind_mode = strings::to_int32(value("grind_mode"));
    for (int i = 0; i < 3; ++i) {
        settings.profile_weights[i] =
            strings::to_float(value(("weight" + std::to_string(i)).c_str()));
        settings.profile_times[i] =
            strings::to_float(value(("time" + std::to_string(i)).c_str()));
    }
    settings.auto_start = form_bool(value("auto_start"));
    if (has("auto_start_threshold_g")) {
        settings.auto_start_threshold_g = strings::to_float(value("auto_start_threshold_g"));
    } else {
        Preferences auto_preferences;
        if (auto_preferences.begin("autogrind", true)) {
            settings.auto_start_threshold_g = auto_preferences.getFloat(
                "start_delta_g", USER_AUTO_GRIND_TRIGGER_DELTA_G);
            auto_preferences.end();
        }
    }
    settings.auto_return = form_bool(value("auto_return"));
    settings.purge_mode = strings::to_int32(value("purge_mode"));
    settings.purge_amount_g = strings::to_float(value("purge_amount_g"));
    settings.freshness_hours = strings::to_float(value("freshness_hours"));
    settings.coast_ratio = strings::to_float(value("coast_ratio"));
    settings.motor_latency_ms = has("motor_latency_ms")
                                    ? strings::to_float(value("motor_latency_ms"))
                                    : grind_controller_->get_motor_response_latency();
    settings.logging_enabled = form_bool(value("logging_enabled"));
    settings.swipe_enabled = form_bool(value("swipe_enabled"));
    settings.brightness_percent = strings::to_int32(value("brightness_percent"));
    settings.screensaver_brightness_percent = strings::to_int32(value("screensaver_brightness_percent"));
    settings.screensaver_startup = form_bool(value("screensaver_startup"));
    settings.screensaver_sleep = form_bool(value("screensaver_sleep"));
    const long screensaver_idle_timeout_s = strings::to_int32(value("screensaver_idle_timeout_s"));
    const long screensaver_startup_timeout_s = strings::to_int32(value("screensaver_startup_timeout_s"));
    // Older web pages predate panel-off settings. Preserve the stored values
    // rather than rejecting the whole form or silently resetting the option.
    settings.has_display_off_enabled = has("display_off_enabled");
    settings.has_display_off_delay_s = has("display_off_delay_s");
    const long display_off_delay_s = settings.has_display_off_delay_s
                                         ? strings::to_int32(value("display_off_delay_s"))
                                         : settings.display_off_delay_s;
    settings.screensaver_idle_timeout_s = static_cast<uint16_t>(screensaver_idle_timeout_s);
    settings.screensaver_startup_timeout_s = static_cast<uint8_t>(screensaver_startup_timeout_s);
    settings.display_off_enabled = settings.has_display_off_enabled
                                       ? form_bool(value("display_off_enabled"))
                                       : settings.display_off_enabled;
    settings.display_off_delay_s = static_cast<uint16_t>(display_off_delay_s);
    const std::string screensaver_style = value("screensaver_style");
    strncpy(settings.screensaver_style, screensaver_style.c_str(), sizeof(settings.screensaver_style) - 1);
    const std::string gaggimate_host = has("gaggimate_host")
                                      ? value("gaggimate_host")
                                      : gaggimate_status_client.configured_host();
    strncpy(settings.gaggimate_host, gaggimate_host.c_str(), sizeof(settings.gaggimate_host) - 1);
    settings.bluetooth_startup = form_bool(value("bluetooth_startup"));

    bool valid = settings.current_profile >= 0 && settings.current_profile < USER_PROFILE_COUNT &&
                 settings.grind_mode >= 0 && settings.grind_mode <= 1 &&
                 std::isfinite(settings.auto_start_threshold_g) &&
                 settings.auto_start_threshold_g >= USER_AUTO_GRIND_TRIGGER_MIN_G &&
                 settings.auto_start_threshold_g <= USER_AUTO_GRIND_TRIGGER_MAX_G &&
                 settings.purge_mode >= 0 && settings.purge_mode <= 1 &&
                 std::isfinite(settings.purge_amount_g) &&
                 settings.purge_amount_g >= GRIND_PURGE_AMOUNT_MIN_G &&
                 settings.purge_amount_g <= GRIND_PURGE_AMOUNT_MAX_G &&
                 std::isfinite(settings.freshness_hours) && settings.freshness_hours >= 0.5f &&
                 settings.freshness_hours <= 48.0f && std::isfinite(settings.coast_ratio) &&
                 settings.coast_ratio >= GRIND_LATENCY_TO_COAST_RATIO_MIN &&
                 settings.coast_ratio <= GRIND_LATENCY_TO_COAST_RATIO_MAX &&
                 std::isfinite(settings.motor_latency_ms) &&
                 settings.motor_latency_ms >= GRIND_AUTOTUNE_LATENCY_MIN_MS &&
                 settings.motor_latency_ms <= GRIND_AUTOTUNE_LATENCY_MAX_MS &&
                 std::fabs(settings.motor_latency_ms / GRIND_MOTOR_LATENCY_MANUAL_STEP_MS -
                           std::round(settings.motor_latency_ms /
                                      GRIND_MOTOR_LATENCY_MANUAL_STEP_MS)) < 0.001f &&
                 settings.brightness_percent >= HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT &&
                 settings.brightness_percent <= 100 &&
                 settings.screensaver_brightness_percent >= HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT &&
                 settings.screensaver_brightness_percent <= 100 &&
                 screensaver_idle_timeout_s >= 0 && screensaver_idle_timeout_s <= 65535 &&
                 screensaver_startup_timeout_s >= 0 && screensaver_startup_timeout_s <= 255 &&
                 display_off_delay_s >= 0 && display_off_delay_s <= 65535 &&
                 ScreensaverSettings::is_valid_idle_timeout(settings.screensaver_idle_timeout_s) &&
                 ScreensaverSettings::is_valid_startup_timeout(settings.screensaver_startup_timeout_s) &&
                 ScreensaverSettings::is_valid_display_off_delay(settings.display_off_delay_s) &&
                 (screensaver_style == "orbit" || screensaver_style == "minimal" ||
                  screensaver_style == "blank" ||
                  (screensaver_style == "custom" && filesystem.exists(BLE_IMAGE_FILENAME)) ||
                  (screensaver_style == "gaggimate" && GaggiMateStatusClient::is_valid_host(gaggimate_host)));
    for (int i = 0; i < 3 && valid; ++i) {
        valid = std::isfinite(settings.profile_weights[i]) &&
                profile_controller_->is_weight_valid(settings.profile_weights[i]) &&
                std::isfinite(settings.profile_times[i]) &&
                profile_controller_->is_time_valid(settings.profile_times[i]);
    }
    if (!valid) {
        return http::send_error(request, 400, "One or more settings are outside the supported range");
    }
    if (grind_controller_->is_active()) {
        return http::send_error(request, 409, "Stop the grinder before changing settings");
    }

    Command command{};
    command.action = CommandAction::APPLY_SETTINGS;
    command.settings = settings;
    command.request_id = reserve_settings_result();
    if (!command.request_id) {
        return http::send_error(request, 409, "A settings save is still pending");
    }
    if (xQueueSend(command_queue_, &command, 0) != pdTRUE) {
        set_settings_result(command.request_id, "failed");
        return http::send_error(request, 503, "Settings queue is busy; try again");
    }
    return http::send_json(request, 202, "{\"accepted\":true,\"request_id\":" +
                                             std::to_string(command.request_id) + "}");
}

bool DeviceApi::apply_settings(const DeviceSettingsUpdate& update) {
    if (!hardware_ || !profile_controller_ || !grind_controller_ || grind_controller_->is_active()) return false;
    DeviceSettingsUpdate settings = update;
    // Resolve omissions when executing, after earlier queued saves have applied.
    if (!settings.has_display_off_enabled || !settings.has_display_off_delay_s) {
        const auto stored_timing = ScreensaverSettings::load_timing();
        if (!settings.has_display_off_enabled) settings.display_off_enabled = stored_timing.display_off_enabled;
        if (!settings.has_display_off_delay_s) settings.display_off_delay_s = stored_timing.display_off_delay_s;
    }
    Preferences* grinder = hardware_->get_preferences();
    if (!grinder) return false;
    bool saved = profile_controller_->apply_web_settings(
            settings.current_profile,
            settings.grind_mode == 0 ? GrindMode::WEIGHT : GrindMode::TIME,
            settings.profile_weights, settings.profile_times);

    saved = grinder->putInt(GrindController::PREF_KEY_GRINDER_MODE, settings.purge_mode) && saved;
    saved = grinder->putFloat(GrindController::PREF_KEY_GRINDER_AMOUNT_G, settings.purge_amount_g) && saved;
    saved = grinder->putFloat(GrindController::PREF_KEY_GRIND_FRESHNESS_HOURS, settings.freshness_hours) && saved;
    saved = grind_controller_->save_coast_ratio(settings.coast_ratio) && saved;
    saved = grind_controller_->save_motor_latency(settings.motor_latency_ms) && saved;

    auto put_bool = [](const char* name_space, const char* key, bool value) {
        Preferences preferences;
        if (!preferences.begin(name_space, false)) return false;
        const bool stored = preferences.putBool(key, value);
        preferences.end();
        return stored;
    };
    saved = put_bool("autogrind", "auto_start", settings.auto_start) && saved;
    saved = put_bool("autogrind", "auto_return", settings.auto_return) && saved;
    Preferences auto_preferences;
    if (auto_preferences.begin("autogrind", false)) {
        saved = auto_preferences.putFloat("start_delta_g", settings.auto_start_threshold_g) && saved;
        auto_preferences.end();
    } else {
        saved = false;
    }
    saved = put_bool("logging", "enabled", settings.logging_enabled) && saved;
    saved = put_bool("swipe", "enabled", settings.swipe_enabled) && saved;
    saved = put_bool("screensaver", "startup", settings.screensaver_startup) && saved;
    saved = put_bool("screensaver", "sleep", settings.screensaver_sleep) && saved;
    saved = put_bool("bluetooth", "startup", settings.bluetooth_startup) && saved;

    Preferences brightness;
    if (brightness.begin("brightness", false)) {
        const bool normal_saved = brightness.putFloat("normal", settings.brightness_percent / 100.0f);
        saved = normal_saved && saved;
        saved = brightness.putFloat("screensaver", settings.screensaver_brightness_percent / 100.0f) && saved;
        brightness.end();
        if (normal_saved && hardware_->get_display()) {
            hardware_->get_display()->set_brightness(settings.brightness_percent / 100.0f);
        }
    } else {
        saved = false;
    }
    if (!ScreensaverSettings::save_timing(settings.screensaver_idle_timeout_s,
                                          settings.screensaver_startup_timeout_s,
                                          settings.display_off_enabled,
                                          settings.display_off_delay_s)) {
        saved = false;
    }
    Preferences screensaver;
    if (screensaver.begin("screensaver", false)) {
        const bool style_saved = screensaver.putString("style", settings.screensaver_style);
        saved = style_saved && saved;
        const std::string stored_style = screensaver.getString("style", "");
        screensaver.end();
        const bool style_read = stored_style == "minimal" || stored_style == "orbit" ||
                                stored_style == "blank" || stored_style == "custom" ||
                                stored_style == "gaggimate";
        if (style_read) {
            saved = gaggimate_status_client.configure(stored_style == "gaggimate",
                                                      settings.gaggimate_host) && saved;
        } else {
            // Keep the active client unchanged when stored style is unreadable.
            saved = false;
        }
    } else {
        saved = false;
    }
    LOG_BLE("[WEB] Grinder settings %s\n", saved ? "updated" : "partly saved; storage or runtime setup failed");
    return saved;
}

std::string DeviceApi::settings_json() {
    if (!settings_mutex_) return "{}";
    xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    const std::string copy = settings_json_cache_;
    xSemaphoreGive(settings_mutex_);
    return copy;
}

void DeviceApi::refresh_settings_cache() {
    if (!profile_controller_ || !hardware_ || !grind_controller_ || !settings_mutex_) return;
    const auto profile = profile_controller_->snapshot();
    auto read_bool = [](const char* name_space, const char* key, bool fallback) {
        Preferences preferences;
        if (!preferences.begin(name_space, true)) return fallback;
        const bool value = preferences.getBool(key, fallback);
        preferences.end();
        return value;
    };
    auto read_float = [](const char* name_space, const char* key, float fallback) {
        Preferences preferences;
        if (!preferences.begin(name_space, true)) return fallback;
        const float value = preferences.getFloat(key, fallback);
        preferences.end();
        return value;
    };
    Preferences* grinder = hardware_->get_preferences();
    const int purge_mode = grinder->getInt(GrindController::PREF_KEY_GRINDER_MODE, GRIND_PURGE_MODE_DEFAULT);
    const float purge_amount = grinder->getFloat(GrindController::PREF_KEY_GRINDER_AMOUNT_G, GRIND_PURGE_AMOUNT_DEFAULT_G);
    const float freshness = grinder->getFloat(GrindController::PREF_KEY_GRIND_FRESHNESS_HOURS, GRIND_FRESHNESS_DEFAULT_HOURS);
    const ScreensaverTimingSettings screensaver_timing = ScreensaverSettings::load_timing();
    Preferences screensaver_preferences;
    std::string screensaver_style = filesystem.exists(BLE_IMAGE_FILENAME) ? "custom" : "minimal";
    if (screensaver_preferences.begin("screensaver", true)) {
        screensaver_style = screensaver_preferences.getString("style", screensaver_style);
        screensaver_preferences.end();
    }
    const std::string gaggimate_host = gaggimate_status_client.configured_host();

    char json[2048];
    snprintf(json, sizeof(json),
             "{\"api\":\"v1\",\"current_profile\":%d,\"grind_mode\":\"%s\","
             "\"profiles\":["
             "{\"id\":0,\"name\":\"%s\",\"weight\":%.2f,\"time\":%.2f},"
             "{\"id\":1,\"name\":\"%s\",\"weight\":%.2f,\"time\":%.2f},"
             "{\"id\":2,\"name\":\"%s\",\"weight\":%.2f,\"time\":%.2f}],"
             "\"automatic\":{\"start\":%s,\"threshold_g\":%.1f,\"return\":%s},"
             "\"purge\":{\"mode\":%d,\"amount_g\":%.2f,\"freshness_hours\":%.2f},"
             "\"coast_ratio\":%.2f,\"motor_latency_ms\":%.0f,"
             "\"logging_enabled\":%s,\"swipe_enabled\":%s,"
             "\"display\":{\"brightness\":%d,\"screensaver_brightness\":%d,"
             "\"screensaver_startup\":%s,\"screensaver_sleep\":%s,"
             "\"screensaver_idle_timeout_s\":%u,\"screensaver_startup_timeout_s\":%u,"
             "\"display_off_enabled\":%s,\"display_off_delay_s\":%u,"
             "\"screensaver_style\":\"%s\",\"has_custom_screensaver\":%s,"
             "\"gaggimate_host\":\"%s\"},"
             "\"bluetooth_startup\":%s}",
             profile.current_profile,
             profile.mode == GrindMode::TIME ? "time" : "weight",
             profile.profiles[0].name, profile.profiles[0].weight, profile.profiles[0].time_seconds,
             profile.profiles[1].name, profile.profiles[1].weight, profile.profiles[1].time_seconds,
             profile.profiles[2].name, profile.profiles[2].weight, profile.profiles[2].time_seconds,
             read_bool("autogrind", "auto_start", false) ? "true" : "false",
             std::clamp(read_float("autogrind", "start_delta_g", USER_AUTO_GRIND_TRIGGER_DELTA_G),
                        USER_AUTO_GRIND_TRIGGER_MIN_G, USER_AUTO_GRIND_TRIGGER_MAX_G),
             read_bool("autogrind", "auto_return", false) ? "true" : "false",
             purge_mode, purge_amount, freshness, grind_controller_->get_coast_ratio(),
             grind_controller_->get_motor_response_latency(),
             read_bool("logging", "enabled", true) ? "true" : "false",
             read_bool("swipe", "enabled", false) ? "true" : "false",
             static_cast<int>(read_float("brightness", "normal", USER_SCREEN_BRIGHTNESS_NORMAL) * 100.0f + 0.5f),
             static_cast<int>(read_float("brightness", "screensaver", USER_SCREEN_BRIGHTNESS_DIMMED) * 100.0f + 0.5f),
             read_bool("screensaver", "startup", false) ? "true" : "false",
             read_bool("screensaver", "sleep", false) ? "true" : "false",
             screensaver_timing.idle_timeout_s, screensaver_timing.startup_timeout_s,
             screensaver_timing.display_off_enabled ? "true" : "false",
             screensaver_timing.display_off_delay_s,
             screensaver_style.c_str(), filesystem.exists(BLE_IMAGE_FILENAME) ? "true" : "false",
             gaggimate_host.c_str(),
             read_bool("bluetooth", "startup", true) ? "true" : "false");
    xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    settings_json_cache_ = json;
    xSemaphoreGive(settings_mutex_);
}

void DeviceApi::send_ack(int client_fd, uint32_t request_id, bool has_request_id,
                         const char* action, bool accepted, const char* reason) {
    if (client_fd == NO_CLIENT) return;
    char message[192];
    if (has_request_id) {
        snprintf(message, sizeof(message),
                 "{\"api\":\"v1\",\"type\":\"ack\",\"rid\":%lu,\"action\":\"%s\",\"accepted\":%s,\"reason\":\"%s\"}",
                 static_cast<unsigned long>(request_id), action,
                 accepted ? "true" : "false", reason);
    } else {
        snprintf(message, sizeof(message),
                 "{\"api\":\"v1\",\"type\":\"ack\",\"action\":\"%s\",\"accepted\":%s,\"reason\":\"%s\"}",
                 action, accepted ? "true" : "false", reason);
    }
    send_text(client_fd, message);
}

void DeviceApi::send_ack(const Command& command, const char* action, bool accepted,
                         const char* reason) {
    send_ack(command.client_fd, command.request_id, command.has_request_id,
             action, accepted, reason);
}

std::string DeviceApi::build_state_message() {
    // Capture profile data before taking controller access; profile persistence
    // must not hold up the motor control loop through a network reader.
    const auto profile = profile_controller_->snapshot();
    const auto control_lock = grind_controller_->lock_control();
    WeightSensor* sensor = hardware_->get_weight_sensor();
    Grinder* grinder = hardware_->get_grinder();
    const bool idle = grind_controller_->get_phase() == GrindPhase::IDLE;
    const GrindMode mode = idle ? profile.mode : grind_controller_->get_mode();
    const float target_weight = idle ? profile.profiles[profile.current_profile].weight
                                     : grind_controller_->get_target_weight();
    const uint32_t target_time_ms = idle
        ? static_cast<uint32_t>(profile.profiles[profile.current_profile].time_seconds * 1000.0f)
        : grind_controller_->get_target_time_ms();
    // The web metric is for a human-readable live display, so mirror the
    // steadier filtered value used by the on-device UI. The high-frequency raw
    // control samples remain available in the saved session trace.
    const float weight = sensor ? sensor->get_display_weight() : 0.0f;
    const float flow = sensor ? sensor->get_flow_rate() : 0.0f;
    const bool motor_running = grinder && grinder->is_grinding();

    char message[640];
    snprintf(message, sizeof(message),
             "{\"api\":\"v1\",\"type\":\"state\",\"seq\":%lu,\"timestamp_ms\":%lu,"
             "\"grind\":{\"active\":%s,\"phase\":\"%s\",\"mode\":\"%s\",\"profile\":%d,\"progress\":%d,"
             "\"target_weight\":%.2f,\"target_time_ms\":%lu},"
             "\"scale\":{\"weight\":%.2f,\"flow\":%.2f},"
             "\"motor\":{\"running\":%s},\"system\":{\"free_heap\":%u}}",
             static_cast<unsigned long>(sequence_.fetch_add(1) + 1), static_cast<unsigned long>(millis()),
             grind_controller_->is_active() ? "true" : "false",
             api_phase_name(*grind_controller_),
             mode == GrindMode::TIME ? "time" : mode == GrindMode::MANUAL ? "manual" : "weight",
             idle ? profile.current_profile : grind_controller_->get_session_descriptor().profile_id,
             grind_controller_->get_current_progress_percent(),
             target_weight,
             static_cast<unsigned long>(target_time_ms),
             weight, flow, motor_running ? "true" : "false",
             static_cast<unsigned int>(device_info::free_heap_bytes()));
    return std::string(message);
}
