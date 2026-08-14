#include "device_api.h"

#include <cstring>

#include "../controllers/grind_controller.h"
#include "../hardware/WeightSensor.h"
#include "../hardware/grinder.h"
#include "../hardware/hardware_manager.h"

DeviceApi device_api;

namespace {
bool contains_json_string(const char* json, const char* key, const char* value) {
    if (!json || !key || !value) return false;
    String needle = "\"" + String(key) + "\":\"" + String(value) + "\"";
    return strstr(json, needle.c_str()) != nullptr;
}

bool websocket_origin_allowed(AsyncWebServerRequest* request) {
    if (!request || !request->hasHeader("Origin")) return true;
    const AsyncWebHeader* origin_header = request->getHeader("Origin");
    if (!origin_header) return false;
    return origin_header->value() == ("http://" + request->host());
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
                     GrindController* grind_controller) {
    if (initialized_ || !server || !hardware || !grind_controller) return;
    hardware_ = hardware;
    grind_controller_ = grind_controller;
    command_queue_ = xQueueCreate(8, sizeof(Command));
    if (!command_queue_) {
        hardware_ = nullptr;
        grind_controller_ = nullptr;
        return;
    }
    websocket_.onEvent([this](AsyncWebSocket* ws, AsyncWebSocketClient* client,
                              AwsEventType type, void* arg, uint8_t* data, size_t len) {
        handle_event(ws, client, type, arg, data, len);
    });
    websocket_.handleHandshake(websocket_origin_allowed);
    server->addHandler(&websocket_);
    initialized_ = true;
}

void DeviceApi::update() {
    if (!initialized_) return;
    websocket_.cleanupClients(MAX_CLIENTS);
    const uint32_t now = millis();
    if (now - last_publish_ms_ < PUBLISH_INTERVAL_MS) return;
    last_publish_ms_ = now;

    const String message = build_state_message();
    for (auto& slot : client_ids_) {
        const uint32_t id = slot.load();
        if (id == 0) continue;
        if (!websocket_.hasClient(id)) {
            slot.store(0);
            continue;
        }
        if (!websocket_.availableForWrite(id)) {
            websocket_.close(id, 1013, "client too slow");
            slot.store(0);
            continue;
        }
        websocket_.text(id, message);
    }
}

void DeviceApi::process_commands() {
    if (!initialized_) return;
    Command command{};
    while (xQueueReceive(command_queue_, &command, 0) == pdTRUE) {
        switch (command.action) {
            case CommandAction::STOP:
                if (!grind_controller_->is_active()) {
                    send_ack(command.client_id, "stop", false, "grinder is not active");
                } else {
                    grind_controller_->stop_grind();
                    send_ack(command.client_id, "stop", true, "grind stopped");
                }
                break;
            case CommandAction::DISMISS:
                if (grind_controller_->get_phase() != GrindPhase::COMPLETED &&
                    grind_controller_->get_phase() != GrindPhase::TIMEOUT) {
                    send_ack(command.client_id, "dismiss", false, "nothing to dismiss");
                } else {
                    grind_controller_->return_to_idle();
                    send_ack(command.client_id, "dismiss", true, "result dismissed");
                }
                break;
        }
    }
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
    for (auto& slot : client_ids_) {
        uint32_t empty = 0;
        if (slot.compare_exchange_strong(empty, client->id())) {
            client->keepAlivePeriod(20);
            client->text(build_state_message());
            return;
        }
    }
    client->close(1013, "too many clients");
}

void DeviceApi::remove_client(uint32_t client_id) {
    for (auto& slot : client_ids_) {
        uint32_t expected = client_id;
        slot.compare_exchange_strong(expected, 0);
    }
}

void DeviceApi::queue_command(uint32_t client_id, const uint8_t* data, size_t len) {
    char json[256];
    memcpy(json, data, len);
    json[len] = '\0';
    if (!contains_json_string(json, "type", "command")) {
        send_ack(client_id, "unknown", false, "invalid message type");
        return;
    }

    Command command{client_id, CommandAction::STOP};
    const char* action = nullptr;
    if (contains_json_string(json, "action", "stop")) {
        action = "stop";
        command.action = CommandAction::STOP;
    } else if (contains_json_string(json, "action", "dismiss")) {
        action = "dismiss";
        command.action = CommandAction::DISMISS;
    } else {
        send_ack(client_id, "unknown", false, "unsupported action");
        return;
    }

    if (xQueueSend(command_queue_, &command, 0) != pdTRUE) {
        send_ack(client_id, action, false, "command queue busy");
    }
}

void DeviceApi::send_ack(uint32_t client_id, const char* action, bool accepted,
                         const char* reason) {
    if (!websocket_.hasClient(client_id) || !websocket_.availableForWrite(client_id)) return;
    char message[192];
    snprintf(message, sizeof(message),
             "{\"api\":\"v1\",\"type\":\"ack\",\"action\":\"%s\",\"accepted\":%s,\"reason\":\"%s\"}",
             action, accepted ? "true" : "false", reason);
    websocket_.text(client_id, message);
}

String DeviceApi::build_state_message() {
    WeightSensor* sensor = hardware_->get_weight_sensor();
    Grinder* grinder = hardware_->get_grinder();
    const GrindMode mode = grind_controller_->get_mode();
    const float weight = sensor ? sensor->get_weight_low_latency() : 0.0f;
    const float flow = sensor ? sensor->get_flow_rate() : 0.0f;
    const bool motor_running = grinder && grinder->is_grinding();

    char message[640];
    snprintf(message, sizeof(message),
             "{\"api\":\"v1\",\"type\":\"state\",\"seq\":%lu,\"timestamp_ms\":%lu,"
             "\"grind\":{\"active\":%s,\"phase\":\"%s\",\"mode\":\"%s\",\"progress\":%d,"
             "\"target_weight\":%.2f,\"target_time_ms\":%lu},"
             "\"scale\":{\"weight\":%.2f,\"flow\":%.2f},"
             "\"motor\":{\"running\":%s},\"system\":{\"free_heap\":%u}}",
             static_cast<unsigned long>(sequence_.fetch_add(1) + 1), static_cast<unsigned long>(millis()),
             grind_controller_->is_active() ? "true" : "false",
             api_phase_name(*grind_controller_),
             mode == GrindMode::TIME ? "time" : "weight",
             grind_controller_->get_current_progress_percent(),
             grind_controller_->get_target_weight(),
             static_cast<unsigned long>(grind_controller_->get_target_time_ms()),
             weight, flow, motor_running ? "true" : "false",
             static_cast<unsigned int>(ESP.getFreeHeap()));
    return String(message);
}
