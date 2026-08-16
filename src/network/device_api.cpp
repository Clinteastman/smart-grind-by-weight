#include "device_api.h"

#include <cmath>
#include <cctype>
#include <cstring>
#include <LittleFS.h>

#include "../controllers/grind_controller.h"
#include "../controllers/profile_controller.h"
#include "../config/constants.h"
#include "../hardware/WeightSensor.h"
#include "../hardware/grinder.h"
#include "../hardware/hardware_manager.h"
#include "../system/screensaver_settings.h"
#include "device_web_server.h"
#include "gaggimate_status_client.h"

DeviceApi device_api;

namespace {
bool contains_json_string(const char* json, const char* key, const char* value) {
    if (!json || !key || !value) return false;
    const String needle = "\"" + String(key) + "\"";
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
    const String needle = "\"" + String(key) + "\"";
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

bool websocket_origin_allowed(AsyncWebServerRequest* request) {
    if (!request || !request->hasHeader("Origin")) return true;
    const AsyncWebHeader* origin_header = request->getHeader("Origin");
    if (!origin_header) return false;
    return origin_header->value() == ("http://" + request->host());
}

bool valid_local_host(const String& host) {
    if (host.isEmpty() || host.length() > 63) return false;
    for (size_t index = 0; index < host.length(); ++index) {
        const char value = host[index];
        if (!isalnum(static_cast<unsigned char>(value)) && value != '.' && value != '-') {
            return false;
        }
    }
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

void DeviceApi::init(AsyncWebServer* server, HardwareManager* hardware,
                     GrindController* grind_controller, ProfileController* profile_controller) {
    if (initialized_ || !server || !hardware || !grind_controller || !profile_controller) return;
    hardware_ = hardware;
    grind_controller_ = grind_controller;
    profile_controller_ = profile_controller;
    command_queue_ = xQueueCreate(8, sizeof(Command));
    settings_mutex_ = xSemaphoreCreateMutex();
    if (!command_queue_ || !settings_mutex_) {
        hardware_ = nullptr;
        grind_controller_ = nullptr;
        profile_controller_ = nullptr;
        return;
    }
    websocket_.onEvent([this](AsyncWebSocket* ws, AsyncWebSocketClient* client,
                              AwsEventType type, void* arg, uint8_t* data, size_t len) {
        handle_event(ws, client, type, arg, data, len);
    });
    websocket_.handleHandshake(websocket_origin_allowed);
    server->addHandler(&websocket_);
    configure_settings_routes(server);
    refresh_settings_cache();
    initialized_ = true;
}

bool form_bool(const String& value) {
    return value == "1" || value == "true" || value == "on";
}

void DeviceApi::update() {
    if (!initialized_) return;
    if (settings_cache_dirty_.exchange(false)) refresh_settings_cache();
    websocket_.cleanupClients(MAX_CLIENTS);
    const uint32_t now = millis();
    if (now - last_publish_ms_ < PUBLISH_INTERVAL_MS) return;
    last_publish_ms_ = now;

    const String message = build_state_message();
    for (size_t index = 0; index < MAX_CLIENTS; ++index) {
        auto& slot = client_ids_[index];
        const uint32_t id = slot.load();
        if (id == 0) {
            backpressure_skips_[index].store(0);
            continue;
        }
        if (!websocket_.hasClient(id)) {
            slot.store(0);
            backpressure_skips_[index].store(0);
            continue;
        }
        if (!websocket_.availableForWrite(id)) {
            uint8_t skipped = backpressure_skips_[index].load();
            if (skipped < MAX_CONSECUTIVE_BACKPRESSURE_SKIPS) ++skipped;
            backpressure_skips_[index].store(skipped);
            if (skipped >= MAX_CONSECUTIVE_BACKPRESSURE_SKIPS) {
                LOG_BLE("[WEB] Closing WebSocket client %lu after sustained backpressure\n",
                        static_cast<unsigned long>(id));
                websocket_.close(id, 1013, "client remained too slow");
                slot.store(0);
                backpressure_skips_[index].store(0);
            }
            continue;
        }
        backpressure_skips_[index].store(0);
        websocket_.text(id, message);
    }
}

bool DeviceApi::process_commands() {
    if (!initialized_) return false;
    bool settings_changed = false;
    Command command{};
    while (xQueueReceive(command_queue_, &command, 0) == pdTRUE) {
        switch (command.action) {
            case CommandAction::START: {
                if (device_web_server.is_ota_active() || device_web_server.is_ota_preparing()) {
                    send_ack(command, "start", false, "firmware update is active");
                    break;
                }
                if (grind_controller_->get_phase() != GrindPhase::IDLE) {
                    send_ack(command, "start", false, "grinder is not idle");
                    break;
                }
                const GrindMode mode = profile_controller_->get_grind_mode();
                WeightSensor* sensor = hardware_->get_weight_sensor();
                if (mode == GrindMode::WEIGHT &&
                    (!sensor || sensor->has_hardware_fault())) {
                    send_ack(command, "start", false, "load cell is not ready");
                    break;
                }
                grind_controller_->set_grind_profile_id(profile_controller_->get_current_profile());
                const float target_weight = profile_controller_->get_current_weight();
                const uint32_t target_time_ms = static_cast<uint32_t>(
                    profile_controller_->get_current_time() * 1000.0f + 0.5f);
                grind_controller_->start_grind(target_weight, target_time_ms, mode);
                send_ack(command, "start", true, "grind started");
                break;
            }
            case CommandAction::START_MANUAL:
                if (device_web_server.is_ota_active() || device_web_server.is_ota_preparing()) {
                    send_ack(command, "start_manual", false, "firmware update is active");
                } else if (grind_controller_->get_phase() != GrindPhase::IDLE) {
                    send_ack(command, "start_manual", false, "grinder is not idle");
                } else {
                    grind_controller_->start_grind(0.0f, 0, GrindMode::MANUAL);
                    send_ack(command, "start_manual", true, "manual grind started");
                }
                break;
            case CommandAction::STOP:
                if (!grind_controller_->is_active()) {
                    send_ack(command, "stop", false, "grinder is not active");
                } else {
                    grind_controller_->stop_grind();
                    send_ack(command, "stop", true, "grind stopped");
                }
                break;
            case CommandAction::DISMISS:
                if (grind_controller_->get_phase() != GrindPhase::COMPLETED &&
                    grind_controller_->get_phase() != GrindPhase::TIMEOUT) {
                    send_ack(command, "dismiss", false, "nothing to dismiss");
                } else {
                    grind_controller_->return_to_idle();
                    send_ack(command, "dismiss", true, "result dismissed");
                }
                break;
            case CommandAction::TARE: {
                WeightSensor* sensor = hardware_->get_weight_sensor();
                if (grind_controller_->get_phase() != GrindPhase::IDLE) {
                    send_ack(command, "tare", false, "grinder is not idle");
                } else if (!sensor || sensor->has_hardware_fault() || sensor->get_sample_count() <= 0) {
                    send_ack(command, "tare", false, "load cell is not ready");
                } else if (sensor->is_tare_in_progress()) {
                    send_ack(command, "tare", false, "tare is already in progress");
                } else {
                    sensor->tareNoDelay();
                    send_ack(command, "tare", true, "tare started");
                }
                break;
            }
            case CommandAction::SELECT_PROFILE:
                if (grind_controller_->get_phase() != GrindPhase::IDLE) {
                    send_ack(command, "select_profile", false, "grinder is not idle");
                    break;
                }
                profile_controller_->set_current_profile(command.profile_index);
                settings_changed = true;
                LOG_BLE("[WEB] Active profile changed to %s\n",
                        profile_controller_->get_current_name());
                send_ack(command, "select_profile", true, "profile selected");
                break;
            case CommandAction::SET_MODE:
                if (grind_controller_->get_phase() != GrindPhase::IDLE) {
                    send_ack(command, "set_mode", false, "grinder is not idle");
                    break;
                }
                profile_controller_->set_grind_mode(
                    command.grind_mode == 1 ? GrindMode::TIME : GrindMode::WEIGHT);
                settings_changed = true;
                send_ack(command, "set_mode", true, "grind mode selected");
                break;
            case CommandAction::APPLY_SETTINGS:
                if (apply_settings(command.settings)) {
                    settings_changed = true;
                }
                break;
        }
    }
    if (settings_changed) refresh_settings_cache();
    return settings_changed;
}

void DeviceApi::handle_event(AsyncWebSocket*, AsyncWebSocketClient* client,
                             AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (!client) return;
    if (type == WS_EVT_CONNECT) {
        add_client(client);
        return;
    }
    if (type == WS_EVT_DISCONNECT || type == WS_EVT_ERROR) {
        remove_client(client->id());
        return;
    }
    if (type != WS_EVT_DATA || !arg || !data) return;
    auto* info = static_cast<AwsFrameInfo*>(arg);
    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT || len > 255) {
        client->close(1009, "single text frames only");
        return;
    }
    queue_command(client->id(), data, len);
}

void DeviceApi::add_client(AsyncWebSocketClient* client) {
    for (size_t index = 0; index < MAX_CLIENTS; ++index) {
        auto& slot = client_ids_[index];
        uint32_t empty = 0;
        if (slot.compare_exchange_strong(empty, client->id())) {
            backpressure_skips_[index].store(0);
            client->keepAlivePeriod(20);
            client->text(build_state_message());
            return;
        }
    }
    client->close(1013, "too many clients");
}

void DeviceApi::remove_client(uint32_t client_id) {
    for (size_t index = 0; index < MAX_CLIENTS; ++index) {
        auto& slot = client_ids_[index];
        uint32_t expected = client_id;
        if (slot.compare_exchange_strong(expected, 0)) {
            backpressure_skips_[index].store(0);
        }
    }
}

void DeviceApi::queue_command(uint32_t client_id, const uint8_t* data, size_t len) {
    char json[256];
    memcpy(json, data, len);
    json[len] = '\0';
    uint32_t request_id = 0;
    const bool has_request_id = extract_json_uint(json, "rid", request_id);
    if (!contains_json_string(json, "type", "command")) {
        send_ack(client_id, request_id, has_request_id, "unknown", false, "invalid message type");
        return;
    }

    Command command{};
    command.client_id = client_id;
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
        send_ack(client_id, request_id, has_request_id, "unknown", false, "unsupported action");
        return;
    }

    if (xQueueSend(command_queue_, &command, 0) != pdTRUE) {
        send_ack(command, action, false, "command queue busy");
    }
}

void DeviceApi::configure_settings_routes(AsyncWebServer* server) {
    server->on(AsyncURIMatcher::exact("/api/v1/profile"), HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!websocket_origin_allowed(request)) {
            request->send(403, "application/json", "{\"error\":\"Request origin is not allowed\"}");
            return;
        }
        queue_profile_selection(request);
    });
    server->on(AsyncURIMatcher::exact("/api/v1/settings"), HTTP_GET, [this](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response = request->beginResponse(200, "application/json", settings_json());
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
    });
    server->on(AsyncURIMatcher::exact("/api/v1/settings"), HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!websocket_origin_allowed(request)) {
            request->send(403, "application/json", "{\"error\":\"Request origin is not allowed\"}");
            return;
        }
        queue_settings_update(request);
    });
}

bool DeviceApi::queue_profile_selection(AsyncWebServerRequest* request) {
    if (!request->hasParam("profile", true)) {
        request->send(400, "application/json", "{\"error\":\"Missing profile\"}");
        return false;
    }
    const String value = request->getParam("profile", true)->value();
    if (value != "0" && value != "1" && value != "2") {
        request->send(400, "application/json", "{\"error\":\"Profile must be 0, 1 or 2\"}");
        return false;
    }
    if (grind_controller_->get_phase() != GrindPhase::IDLE) {
        request->send(409, "application/json", "{\"error\":\"Profiles can only be changed while the grinder is idle\"}");
        return false;
    }

    Command command{};
    command.action = CommandAction::SELECT_PROFILE;
    command.profile_index = value.toInt();
    if (xQueueSend(command_queue_, &command, 0) != pdTRUE) {
        request->send(503, "application/json", "{\"error\":\"Command queue is busy; try again\"}");
        return false;
    }
    request->send(202, "application/json", "{\"accepted\":true}");
    return true;
}

bool DeviceApi::queue_settings_update(AsyncWebServerRequest* request) {
    static const char* required[] = {
        "current_profile", "grind_mode", "weight0", "weight1", "weight2",
        "time0", "time1", "time2", "auto_start", "auto_return", "purge_mode",
        "purge_amount_g", "freshness_hours", "coast_ratio", "logging_enabled",
        "swipe_enabled", "brightness_percent", "screensaver_brightness_percent",
        "screensaver_startup", "screensaver_sleep", "screensaver_idle_timeout_s",
        "screensaver_startup_timeout_s", "screensaver_style", "bluetooth_startup"
    };
    for (const char* field : required) {
        if (!request->hasParam(field, true)) {
            request->send(400, "application/json", String("{\"error\":\"Missing field: ") + field + "\"}");
            return false;
        }
    }

    auto value = [request](const char* name) { return request->getParam(name, true)->value(); };
    DeviceSettingsUpdate settings{};
    settings.current_profile = value("current_profile").toInt();
    settings.grind_mode = value("grind_mode").toInt();
    for (int i = 0; i < 3; ++i) {
        settings.profile_weights[i] = value((String("weight") + i).c_str()).toFloat();
        settings.profile_times[i] = value((String("time") + i).c_str()).toFloat();
    }
    settings.auto_start = form_bool(value("auto_start"));
    settings.auto_return = form_bool(value("auto_return"));
    settings.purge_mode = value("purge_mode").toInt();
    settings.purge_amount_g = value("purge_amount_g").toFloat();
    settings.freshness_hours = value("freshness_hours").toFloat();
    settings.coast_ratio = value("coast_ratio").toFloat();
    settings.logging_enabled = form_bool(value("logging_enabled"));
    settings.swipe_enabled = form_bool(value("swipe_enabled"));
    settings.brightness_percent = value("brightness_percent").toInt();
    settings.screensaver_brightness_percent = value("screensaver_brightness_percent").toInt();
    settings.screensaver_startup = form_bool(value("screensaver_startup"));
    settings.screensaver_sleep = form_bool(value("screensaver_sleep"));
    const long screensaver_idle_timeout_s = value("screensaver_idle_timeout_s").toInt();
    const long screensaver_startup_timeout_s = value("screensaver_startup_timeout_s").toInt();
    settings.screensaver_idle_timeout_s = static_cast<uint16_t>(screensaver_idle_timeout_s);
    settings.screensaver_startup_timeout_s = static_cast<uint8_t>(screensaver_startup_timeout_s);
    const String screensaver_style = value("screensaver_style");
    strncpy(settings.screensaver_style, screensaver_style.c_str(), sizeof(settings.screensaver_style) - 1);
    const String gaggimate_host = request->hasParam("gaggimate_host", true)
                                      ? value("gaggimate_host")
                                      : gaggimate_status_client.configured_host();
    strncpy(settings.gaggimate_host, gaggimate_host.c_str(), sizeof(settings.gaggimate_host) - 1);
    settings.bluetooth_startup = form_bool(value("bluetooth_startup"));

    bool valid = settings.current_profile >= 0 && settings.current_profile < USER_PROFILE_COUNT &&
                 settings.grind_mode >= 0 && settings.grind_mode <= 1 &&
                 settings.purge_mode >= 0 && settings.purge_mode <= 1 &&
                 std::isfinite(settings.purge_amount_g) &&
                 settings.purge_amount_g >= GRIND_PURGE_AMOUNT_MIN_G &&
                 settings.purge_amount_g <= GRIND_PURGE_AMOUNT_MAX_G &&
                 std::isfinite(settings.freshness_hours) && settings.freshness_hours >= 0.5f &&
                 settings.freshness_hours <= 48.0f && std::isfinite(settings.coast_ratio) &&
                 settings.coast_ratio >= GRIND_LATENCY_TO_COAST_RATIO_MIN &&
                 settings.coast_ratio <= GRIND_LATENCY_TO_COAST_RATIO_MAX &&
                 settings.brightness_percent >= HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT &&
                 settings.brightness_percent <= 100 &&
                 settings.screensaver_brightness_percent >= HW_DISPLAY_MINIMAL_BRIGHTNESS_PERCENT &&
                 settings.screensaver_brightness_percent <= 100 &&
                 screensaver_idle_timeout_s >= 0 && screensaver_idle_timeout_s <= 65535 &&
                 screensaver_startup_timeout_s >= 0 && screensaver_startup_timeout_s <= 255 &&
                 ScreensaverSettings::is_valid_idle_timeout(settings.screensaver_idle_timeout_s) &&
                 ScreensaverSettings::is_valid_startup_timeout(settings.screensaver_startup_timeout_s) &&
                 (screensaver_style == "orbit" || screensaver_style == "minimal" ||
                  screensaver_style == "blank" ||
                  (screensaver_style == "custom" && LittleFS.exists(BLE_IMAGE_FILENAME)) ||
                  (screensaver_style == "gaggimate" && valid_local_host(gaggimate_host)));
    for (int i = 0; i < 3 && valid; ++i) {
        valid = std::isfinite(settings.profile_weights[i]) &&
                profile_controller_->is_weight_valid(settings.profile_weights[i]) &&
                std::isfinite(settings.profile_times[i]) &&
                profile_controller_->is_time_valid(settings.profile_times[i]);
    }
    if (!valid) {
        request->send(400, "application/json", "{\"error\":\"One or more settings are outside the supported range\"}");
        return false;
    }
    if (grind_controller_->is_active()) {
        request->send(409, "application/json", "{\"error\":\"Stop the grinder before changing settings\"}");
        return false;
    }

    Command command{};
    command.action = CommandAction::APPLY_SETTINGS;
    command.settings = settings;
    if (xQueueSend(command_queue_, &command, 0) != pdTRUE) {
        request->send(503, "application/json", "{\"error\":\"Settings queue is busy; try again\"}");
        return false;
    }
    request->send(202, "application/json", "{\"accepted\":true}");
    return true;
}

bool DeviceApi::apply_settings(const DeviceSettingsUpdate& settings) {
    if (!hardware_ || !profile_controller_ || !grind_controller_ || grind_controller_->is_active()) return false;
    if (!profile_controller_->apply_web_settings(
            settings.current_profile,
            settings.grind_mode == 0 ? GrindMode::WEIGHT : GrindMode::TIME,
            settings.profile_weights, settings.profile_times)) {
        return false;
    }

    Preferences* grinder = hardware_->get_preferences();
    if (!grinder) return false;
    grinder->putInt(GrindController::PREF_KEY_GRINDER_MODE, settings.purge_mode);
    grinder->putFloat(GrindController::PREF_KEY_GRINDER_AMOUNT_G, settings.purge_amount_g);
    grinder->putFloat(GrindController::PREF_KEY_GRIND_FRESHNESS_HOURS, settings.freshness_hours);
    grind_controller_->save_coast_ratio(settings.coast_ratio);

    auto put_bool = [](const char* name_space, const char* key, bool value) {
        Preferences preferences;
        if (!preferences.begin(name_space, false)) return;
        preferences.putBool(key, value);
        preferences.end();
    };
    put_bool("autogrind", "auto_start", settings.auto_start);
    put_bool("autogrind", "auto_return", settings.auto_return);
    put_bool("logging", "enabled", settings.logging_enabled);
    put_bool("swipe", "enabled", settings.swipe_enabled);
    put_bool("screensaver", "startup", settings.screensaver_startup);
    put_bool("screensaver", "sleep", settings.screensaver_sleep);
    put_bool("bluetooth", "startup", settings.bluetooth_startup);

    Preferences brightness;
    if (brightness.begin("brightness", false)) {
        brightness.putFloat("normal", settings.brightness_percent / 100.0f);
        brightness.putFloat("screensaver", settings.screensaver_brightness_percent / 100.0f);
        brightness.end();
    }
    if (!ScreensaverSettings::save_timing(settings.screensaver_idle_timeout_s,
                                          settings.screensaver_startup_timeout_s)) {
        return false;
    }
    Preferences screensaver;
    if (screensaver.begin("screensaver", false)) {
        screensaver.putString("style", settings.screensaver_style);
        screensaver.end();
    }
    if (!gaggimate_status_client.configure(
            strcmp(settings.screensaver_style, "gaggimate") == 0,
            settings.gaggimate_host)) {
        return false;
    }
    hardware_->get_display()->set_brightness(settings.brightness_percent / 100.0f);
    LOG_BLE("[WEB] Grinder settings updated\n");
    return true;
}

String DeviceApi::settings_json() {
    if (!settings_mutex_) return "{}";
    xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    const String copy = settings_json_cache_;
    xSemaphoreGive(settings_mutex_);
    return copy;
}

void DeviceApi::refresh_settings_cache() {
    if (!profile_controller_ || !hardware_ || !grind_controller_ || !settings_mutex_) return;
    auto read_bool = [](const char* name_space, const char* key, bool fallback) {
        Preferences preferences;
        if (!preferences.begin(name_space, true)) return fallback;
        const bool value = preferences.getBool(key, fallback);
        preferences.end();
        return value;
    };
    auto read_brightness = [](const char* key, float fallback) {
        Preferences preferences;
        if (!preferences.begin("brightness", true)) return fallback;
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
    String screensaver_style = LittleFS.exists(BLE_IMAGE_FILENAME) ? "custom" : "minimal";
    if (screensaver_preferences.begin("screensaver", true)) {
        screensaver_style = screensaver_preferences.getString("style", screensaver_style);
        screensaver_preferences.end();
    }
    const String gaggimate_host = gaggimate_status_client.configured_host();

    char json[2048];
    snprintf(json, sizeof(json),
             "{\"api\":\"v1\",\"current_profile\":%d,\"grind_mode\":\"%s\","
             "\"profiles\":["
             "{\"id\":0,\"name\":\"%s\",\"weight\":%.2f,\"time\":%.2f},"
             "{\"id\":1,\"name\":\"%s\",\"weight\":%.2f,\"time\":%.2f},"
             "{\"id\":2,\"name\":\"%s\",\"weight\":%.2f,\"time\":%.2f}],"
             "\"automatic\":{\"start\":%s,\"return\":%s},"
             "\"purge\":{\"mode\":%d,\"amount_g\":%.2f,\"freshness_hours\":%.2f},"
             "\"coast_ratio\":%.2f,\"logging_enabled\":%s,\"swipe_enabled\":%s,"
             "\"display\":{\"brightness\":%d,\"screensaver_brightness\":%d,"
             "\"screensaver_startup\":%s,\"screensaver_sleep\":%s,"
             "\"screensaver_idle_timeout_s\":%u,\"screensaver_startup_timeout_s\":%u,"
             "\"screensaver_style\":\"%s\",\"has_custom_screensaver\":%s,"
             "\"gaggimate_host\":\"%s\"},"
             "\"bluetooth_startup\":%s}",
             profile_controller_->get_current_profile(),
             profile_controller_->get_grind_mode() == GrindMode::TIME ? "time" : "weight",
             profile_controller_->get_profile_name(0), profile_controller_->get_profile_weight(0), profile_controller_->get_profile_time(0),
             profile_controller_->get_profile_name(1), profile_controller_->get_profile_weight(1), profile_controller_->get_profile_time(1),
             profile_controller_->get_profile_name(2), profile_controller_->get_profile_weight(2), profile_controller_->get_profile_time(2),
             read_bool("autogrind", "auto_start", false) ? "true" : "false",
             read_bool("autogrind", "auto_return", false) ? "true" : "false",
             purge_mode, purge_amount, freshness, grind_controller_->get_coast_ratio(),
             read_bool("logging", "enabled", true) ? "true" : "false",
             read_bool("swipe", "enabled", false) ? "true" : "false",
             static_cast<int>(read_brightness("normal", USER_SCREEN_BRIGHTNESS_NORMAL) * 100.0f + 0.5f),
             static_cast<int>(read_brightness("screensaver", USER_SCREEN_BRIGHTNESS_DIMMED) * 100.0f + 0.5f),
             read_bool("screensaver", "startup", false) ? "true" : "false",
             read_bool("screensaver", "sleep", false) ? "true" : "false",
             screensaver_timing.idle_timeout_s, screensaver_timing.startup_timeout_s,
             screensaver_style.c_str(), LittleFS.exists(BLE_IMAGE_FILENAME) ? "true" : "false",
             gaggimate_host.c_str(),
             read_bool("bluetooth", "startup", true) ? "true" : "false");
    xSemaphoreTake(settings_mutex_, portMAX_DELAY);
    settings_json_cache_ = json;
    xSemaphoreGive(settings_mutex_);
}

void DeviceApi::send_ack(uint32_t client_id, uint32_t request_id, bool has_request_id,
                         const char* action, bool accepted, const char* reason) {
    if (!websocket_.hasClient(client_id) || !websocket_.availableForWrite(client_id)) return;
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
    websocket_.text(client_id, message);
}

void DeviceApi::send_ack(const Command& command, const char* action, bool accepted,
                         const char* reason) {
    send_ack(command.client_id, command.request_id, command.has_request_id,
             action, accepted, reason);
}

String DeviceApi::build_state_message() {
    WeightSensor* sensor = hardware_->get_weight_sensor();
    Grinder* grinder = hardware_->get_grinder();
    const bool idle = grind_controller_->get_phase() == GrindPhase::IDLE;
    const GrindMode mode = idle ? profile_controller_->get_grind_mode() : grind_controller_->get_mode();
    const float target_weight = idle ? profile_controller_->get_current_weight()
                                     : grind_controller_->get_target_weight();
    const uint32_t target_time_ms = idle
        ? static_cast<uint32_t>(profile_controller_->get_current_time() * 1000.0f)
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
             profile_controller_->get_current_profile(),
             grind_controller_->get_current_progress_percent(),
             target_weight,
             static_cast<unsigned long>(target_time_ms),
             weight, flow, motor_running ? "true" : "false",
             static_cast<unsigned int>(ESP.getFreeHeap()));
    return String(message);
}
