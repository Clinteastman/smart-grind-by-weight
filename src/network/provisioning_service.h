#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

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

    void configure_routes();
    void start();
    void stop_dns();
    void schedule_reboot();
    String load_or_create_ap_password();
    static String build_ap_ssid();
    static String make_random_password(size_t length);
};

extern ProvisioningService provisioning_service;
