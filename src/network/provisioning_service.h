#pragma once

#include <esp_http_server.h>

#include <cstdint>
#include <string>
#include <vector>

#include <improv.h>

#include "../storage/preferences.h"
#include "captive_dns.h"

/**
 * Brings up the Wi-Fi setup access point, its captive portal and the Improv
 * serial provisioning protocol when the grinder has no usable credentials.
 */
class ProvisioningService {
public:
    void init(Preferences* preferences);

    /** Register the setup portal routes on an already-started HTTP server. */
    void attach_routes(httpd_handle_t server);

    void update();

    const std::string& access_point_ssid() const { return ap_ssid_; }
    const std::string& access_point_password() const { return ap_password_; }

private:
    Preferences* preferences_ = nullptr;
    CaptiveDns dns_server_;
    bool initialized_ = false;
    bool active_ = false;
    bool reboot_pending_ = false;
    uint32_t reboot_at_ms_ = 0;
    std::string ap_ssid_;
    std::string ap_password_;
    uint8_t improv_rx_buffer_[266]{};
    size_t improv_rx_position_ = 0;
    uint32_t improv_last_byte_ms_ = 0;
    bool improv_connect_pending_ = false;
    uint32_t improv_connect_started_ms_ = 0;

    void start();
    void stop_dns();
    void schedule_reboot();
    void update_improv_serial();
    bool handle_improv_command(const improv::ImprovCommand& command);
    void send_improv_state(improv::State state);
    void send_improv_error(improv::Error error);
    void send_improv_response(improv::Command command, const std::vector<std::string>& values);
    void send_improv_packet(improv::ImprovSerialType type, const uint8_t* data, size_t length);
    void complete_improv_connection();
    std::string load_or_create_ap_password();
    static std::string build_ap_ssid();
    static std::string make_random_password(size_t length);
};

extern ProvisioningService provisioning_service;
