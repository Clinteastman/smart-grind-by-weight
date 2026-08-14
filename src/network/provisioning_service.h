#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <improv.h>

class ProvisioningService {
public:
    void init(Preferences* preferences, AsyncWebServer* server);
    void update();

    bool is_active() const { return active_; }
    const String& access_point_ssid() const { return ap_ssid_; }
    const String& access_point_password() const { return ap_password_; }

private:
    Preferences* preferences_ = nullptr;
    AsyncWebServer* server_ = nullptr;
    DNSServer dns_server_;
    bool initialized_ = false;
    bool active_ = false;
    bool reboot_pending_ = false;
    uint32_t reboot_at_ms_ = 0;
    String ap_ssid_;
    String ap_password_;
    uint8_t improv_rx_buffer_[266]{};
    size_t improv_rx_position_ = 0;
    uint32_t improv_last_byte_ms_ = 0;
    bool improv_connect_pending_ = false;
    uint32_t improv_connect_started_ms_ = 0;

    void configure_routes();
    void start();
    void stop_dns();
    void schedule_reboot();
    void update_improv_serial();
    bool handle_improv_command(const improv::ImprovCommand& command);
    void send_improv_state(improv::State state);
    void send_improv_error(improv::Error error);
    void send_improv_response(improv::Command command, const std::vector<String>& values);
    void send_improv_packet(improv::ImprovSerialType type, const uint8_t* data, size_t length);
    void complete_improv_connection();
    String load_or_create_ap_password();
    static String build_ap_ssid();
    static String make_random_password(size_t length);
};

extern ProvisioningService provisioning_service;
