#include "provisioning_service.h"

#include <WiFi.h>

#include "../config/constants.h"
#include "network_manager.h"

namespace {
constexpr char SETUP_PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Grind Wi-Fi</title><style>
body{font-family:system-ui,sans-serif;background:#161914;color:#f4f4ed;max-width:34rem;margin:3rem auto;padding:0 1.2rem}
form{background:#242920;padding:1.4rem;border-radius:1rem}label{display:block;margin:.8rem 0 .25rem}
input,button{box-sizing:border-box;width:100%;padding:.8rem;border-radius:.55rem;border:1px solid #68735e;font-size:1rem}
button{margin-top:1.2rem;background:#86a869;color:#10140d;font-weight:700;border:0}small{color:#b8c1b0}
</style></head><body><h1>Connect Smart Grind</h1>
<p>Enter the 2.4 GHz Wi-Fi network used by your phone and Home Assistant.</p>
<form method="post" action="/api/v1/setup/wifi"><label for="ssid">Network name</label>
<input id="ssid" name="ssid" maxlength="32" required autocomplete="off">
<label for="password">Password</label><input id="password" name="password" type="password" maxlength="63">
<small>Leave blank only for an open network.</small><button type="submit">Save and restart</button></form></body></html>
)HTML";

constexpr char DEVICE_PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Grind</title><style>
body{font-family:system-ui,sans-serif;background:#161914;color:#f4f4ed;max-width:34rem;margin:3rem auto;padding:0 1.2rem}
main,section{background:#242920;padding:1.4rem;border-radius:1rem;margin-bottom:1rem}dt{color:#b8c1b0;margin-top:.8rem}dd{margin:.15rem 0;font-size:1.15rem}
input,button{box-sizing:border-box;width:100%;padding:.8rem;border-radius:.55rem;border:1px solid #68735e;font-size:1rem;margin-top:.7rem}
button{background:#86a869;color:#10140d;font-weight:700;border:0}button:disabled{background:#596052;color:#bec4b8}progress{width:100%;margin-top:.8rem}
</style></head><body><main><h1>Smart Grind</h1><p>Connected and ready.</p><dl>
<dt>Firmware</dt><dd id="firmware">Loading...</dd><dt>Network</dt><dd id="network">Loading...</dd>
<dt>Address</dt><dd id="address">Loading...</dd></dl></main>
<section><h2>Firmware update</h2><p id="otaMessage">Arm firmware update from the grinder's Wi-Fi screen first.</p>
<form id="otaForm"><input id="firmwareFile" name="firmware" type="file" accept=".bin,application/octet-stream" required>
<button id="otaButton" type="submit" disabled>Upload firmware</button></form><progress id="otaProgress" max="100" value="0"></progress></section><script>
async function refresh(){try{const s=await fetch('/api/v1/status',{cache:'no-store'}).then(r=>r.json());
firmware.textContent=`${s.firmware.version} (build ${s.firmware.build})`;
network.textContent=s.network.ssid||s.network.state;address.textContent=s.network.ip||'Not connected';
otaProgress.value=s.ota.progress||0;otaButton.disabled=!s.ota.armed||s.ota.active;
otaMessage.textContent=s.ota.active?`Uploading: ${s.ota.progress||0}% — do not remove power.`:
s.ota.armed?`Upload armed for ${s.ota.arm_seconds} seconds.`:"Arm firmware update from the grinder's Wi-Fi screen first.";
}catch(e){network.textContent='Unavailable';}}
otaForm.addEventListener('submit',async e=>{e.preventDefault();otaButton.disabled=true;otaMessage.textContent='Uploading — do not remove power.';
try{const s=await fetch('/api/v1/status',{cache:'no-store'}).then(r=>r.json());if(!s.ota.armed||!s.ota.token)throw new Error('Firmware update is not armed.');
const body=new FormData();body.append('firmware',firmwareFile.files[0]);const r=await fetch('/api/v1/ota',{method:'POST',headers:{'X-Smart-Grind-OTA-Token':s.ota.token},body});const t=await r.text();
if(!r.ok)throw new Error(t);otaMessage.textContent=t;}catch(e){otaMessage.textContent=e.message;await refresh();}});
refresh();setInterval(refresh,1000);
</script></body></html>
)HTML";
}

ProvisioningService provisioning_service;

void ProvisioningService::init(Preferences* preferences, AsyncWebServer* server) {
    preferences_ = preferences;
    server_ = server;
    ap_ssid_ = build_ap_ssid();
    ap_password_ = load_or_create_ap_password();
    if (server_) configure_routes();
    initialized_ = true;
}

void ProvisioningService::update() {
    if (!initialized_) return;

    const NetworkState state = network_manager.state();
    if (!active_ && (state == NetworkState::WIFI_NO_CREDENTIALS ||
                     state == NetworkState::WIFI_SETUP_REQUIRED)) {
        start();
    }

    if (active_) dns_server_.processNextRequest();

    if (reboot_pending_ && static_cast<int32_t>(millis() - reboot_at_ms_) >= 0) {
        LOG_BLE("[WIFI] Restarting with saved network credentials\n");
        Serial.flush();
        ESP.restart();
    }
}

void ProvisioningService::configure_routes() {
    server_->on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        const char* page = network_manager.state() == NetworkState::WIFI_SETUP_AP ? SETUP_PAGE : DEVICE_PAGE;
        request->send(200, "text/html", page);
    });
    server_->on("/api/v1/setup/wifi", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (network_manager.state() != NetworkState::WIFI_SETUP_AP) {
            request->send(403, "text/plain", "Wi-Fi setup is not active");
            return;
        }
        if (!request->hasParam("ssid", true) || !request->hasParam("password", true)) {
            request->send(400, "text/plain", "Network name and password fields are required");
            return;
        }
        const String ssid = request->getParam("ssid", true)->value();
        const String password = request->getParam("password", true)->value();
        if (!network_manager.set_credentials(ssid, password)) {
            request->send(400, "text/plain", "Invalid network name or password");
            return;
        }
        request->send(200, "text/html", "<h1>Saved</h1><p>Smart Grind is restarting and will join your network.</p>");
        schedule_reboot();
    });

    const char* captive_paths[] = {
        "/generate_204", "/redirect", "/hotspot-detect.html", "/canonical.html", "/ncsi.txt"
    };
    for (const char* path : captive_paths) {
        server_->on(path, HTTP_ANY, [](AsyncWebServerRequest* request) { request->redirect("/"); });
    }
    server_->on("/connecttest.txt", HTTP_ANY, [](AsyncWebServerRequest* request) { request->redirect("/"); });
    server_->on("/success.txt", HTTP_ANY, [](AsyncWebServerRequest* request) { request->send(200, "text/plain", "success"); });
    server_->on("/wpad.dat", HTTP_ANY, [](AsyncWebServerRequest* request) { request->send(404); });
    server_->onNotFound([](AsyncWebServerRequest* request) {
        if (network_manager.state() == NetworkState::WIFI_SETUP_AP) request->redirect("/");
        else request->send(404, "text/plain", "Not found");
    });
}

void ProvisioningService::start() {
    if (!network_manager.start_setup_access_point(ap_ssid_, ap_password_)) {
        LOG_BLE("[WIFI] Failed to start setup access point\n");
        return;
    }
    dns_server_.setTTL(60);
    dns_server_.start(53, "*", WiFi.softAPIP());
    active_ = true;
    LOG_BLE("[WIFI] Setup page: http://%s/\n", WiFi.softAPIP().toString().c_str());
}

void ProvisioningService::stop_dns() {
    if (!active_) return;
    dns_server_.stop();
    active_ = false;
}

void ProvisioningService::schedule_reboot() {
    stop_dns();
    reboot_pending_ = true;
    reboot_at_ms_ = millis() + 1500;
}

String ProvisioningService::load_or_create_ap_password() {
    if (!preferences_) return make_random_password(12);
    String password = preferences_->getString("wifi_ap_pass", "");
    if (password.length() >= 8) return password;
    password = make_random_password(12);
    preferences_->putString("wifi_ap_pass", password);
    return password;
}

String ProvisioningService::build_ap_ssid() {
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "SmartGrind-%06lx",
             static_cast<unsigned long>(ESP.getEfuseMac() & 0xFFFFFFULL));
    return String(ssid);
}

String ProvisioningService::make_random_password(size_t length) {
    static constexpr char ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    String password;
    password.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        password += ALPHABET[esp_random() % (sizeof(ALPHABET) - 1)];
    }
    return password;
}
