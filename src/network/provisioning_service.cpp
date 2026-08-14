#include "provisioning_service.h"

#include <cstring>
#include <WiFi.h>

#include "../config/build_info.h"
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
*{box-sizing:border-box}body{font-family:system-ui,sans-serif;background:#161914;color:#f4f4ed;max-width:54rem;margin:1.5rem auto;padding:0 1rem}
main,section{background:#242920;padding:1.25rem;border-radius:1rem;margin-bottom:1rem}.top{display:flex;justify-content:space-between;align-items:center;gap:1rem}
.live{color:#8fc971}.offline{color:#e5a45d}.metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:.7rem;margin:1rem 0}.metric{background:#191d17;padding:.85rem;border-radius:.7rem}
.metric span{display:block;color:#b8c1b0;font-size:.82rem}.metric strong{font-size:1.6rem}canvas{display:block;width:100%;height:220px;background:#11140f;border-radius:.7rem}
input,button{width:100%;padding:.8rem;border-radius:.55rem;border:1px solid #68735e;font-size:1rem;margin-top:.7rem}button{background:#86a869;color:#10140d;font-weight:700;border:0}
button.danger{background:#c65f54;color:#fff}button:disabled{background:#596052;color:#bec4b8}progress{width:100%;margin-top:.8rem}.actions{display:grid;grid-template-columns:1fr 1fr;gap:.7rem}.muted{color:#b8c1b0}
@media(max-width:520px){.metrics{grid-template-columns:1fr 1fr}.actions{grid-template-columns:1fr}}
</style></head><body><main><div class="top"><div><h1>Smart Grind</h1><div id="connection" class="offline">Connecting…</div></div><div id="phase">—</div></div>
<div class="metrics"><div class="metric"><span>Weight</span><strong id="weight">—</strong></div><div class="metric"><span>Flow</span><strong id="flow">—</strong></div><div class="metric"><span>Progress</span><strong id="progressText">—</strong></div></div>
<canvas id="chart"></canvas><progress id="grindProgress" max="100" value="0"></progress><p id="target" class="muted">Waiting for grinder state…</p>
<div class="actions"><button id="stopButton" class="danger" disabled>Stop grind</button><button id="dismissButton" disabled>Dismiss result</button></div><p id="commandMessage" class="muted"></p></main>
<section><h2>Device</h2><p><span id="firmware">Loading…</span><br><span id="network">Loading…</span><br><span id="address"></span></p></section>
<section><h2>Firmware update</h2><p id="otaMessage">Arm firmware update from the grinder's Wi-Fi screen first.</p>
<form id="otaForm"><input id="firmwareFile" name="firmware" type="file" accept=".bin,application/octet-stream" required>
<button id="otaButton" type="submit" disabled>Upload firmware</button></form><progress id="otaProgress" max="100" value="0"></progress></section><script>
const $=id=>document.getElementById(id),samples=[];let ws,retry=500;
function draw(){const c=$('chart'),d=devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;if(c.width!==w*d||c.height!==h*d){c.width=w*d;c.height=h*d}const x=c.getContext('2d');x.setTransform(d,0,0,d,0,0);x.clearRect(0,0,w,h);x.strokeStyle='#30372c';x.beginPath();for(let i=1;i<4;i++){x.moveTo(0,h*i/4);x.lineTo(w,h*i/4)}x.stroke();if(samples.length<2)return;let lo=Math.min(...samples),hi=Math.max(...samples);if(hi-lo<2){lo-=1;hi+=1}x.strokeStyle='#8fc971';x.lineWidth=2;x.beginPath();samples.forEach((v,i)=>{const px=i*w/239,py=h-(v-lo)*h/(hi-lo);i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()}
function render(s){const g=s.grind;$('weight').textContent=`${s.scale.weight.toFixed(2)} g`;$('flow').textContent=`${s.scale.flow.toFixed(2)} g/s`;$('phase').textContent=g.phase;$('progressText').textContent=`${g.progress}%`;$('grindProgress').value=g.progress;
$('target').textContent=g.mode==='time'?`Time target ${(g.target_time_ms/1000).toFixed(1)} s`:`Weight target ${g.target_weight.toFixed(1)} g`;$('stopButton').disabled=!g.active;$('dismissButton').disabled=!['COMPLETED','TIMEOUT'].includes(g.phase);samples.push(s.scale.weight);if(samples.length>240)samples.shift();draw()}
function connect(){ws=new WebSocket(`${location.protocol==='https:'?'wss':'ws'}://${location.host}/ws`);ws.onopen=()=>{$('connection').textContent='Live';$('connection').className='live';retry=500};ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.type==='state')render(m);if(m.type==='ack')$('commandMessage').textContent=m.reason};ws.onclose=()=>{$('connection').textContent='Reconnecting…';$('connection').className='offline';setTimeout(connect,retry);retry=Math.min(retry*2,10000)}}
function command(action){if(ws?.readyState===1)ws.send(JSON.stringify({type:'command',action}))}$('stopButton').onclick=()=>command('stop');$('dismissButton').onclick=()=>command('dismiss');window.onresize=draw;
async function refresh(){try{const s=await fetch('/api/v1/status',{cache:'no-store'}).then(r=>r.json());$('firmware').textContent=`${s.firmware.version} (build ${s.firmware.build})`;$('network').textContent=s.network.ssid||s.network.state;$('address').textContent=s.network.ip||'Not connected';
$('otaProgress').value=s.ota.progress||0;$('otaButton').disabled=!s.ota.armed||s.ota.active;$('otaMessage').textContent=s.ota.active?`Uploading: ${s.ota.progress||0}% — do not remove power.`:s.ota.armed?`Upload armed for ${s.ota.arm_seconds} seconds.`:"Arm firmware update from the grinder's Wi-Fi screen first.";}catch(e){$('network').textContent='Unavailable';}}
$('otaForm').addEventListener('submit',async e=>{e.preventDefault();$('otaButton').disabled=true;$('otaMessage').textContent='Uploading — do not remove power.';
try{const s=await fetch('/api/v1/status',{cache:'no-store'}).then(r=>r.json());if(!s.ota.armed||!s.ota.token)throw new Error('Firmware update is not armed.');
const body=new FormData();body.append('firmware',$('firmwareFile').files[0]);const r=await fetch('/api/v1/ota',{method:'POST',headers:{'X-Smart-Grind-OTA-Token':s.ota.token},body});const t=await r.text();if(!r.ok)throw new Error(t);$('otaMessage').textContent=t;}catch(e){$('otaMessage').textContent=e.message;await refresh();}});
connect();refresh();setInterval(refresh,1000);
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

    update_improv_serial();

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

void ProvisioningService::update_improv_serial() {
    constexpr uint32_t FRAME_TIMEOUT_MS = 100;
    constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
    constexpr size_t MAX_BYTES_PER_UPDATE = 64;

    if (improv_rx_position_ > 0 && millis() - improv_last_byte_ms_ > FRAME_TIMEOUT_MS) {
        improv_rx_position_ = 0;
    }

    size_t processed = 0;
    while (Serial.available() > 0 && processed++ < MAX_BYTES_PER_UPDATE) {
        const uint8_t byte = static_cast<uint8_t>(Serial.read());
        if (improv_rx_position_ >= sizeof(improv_rx_buffer_)) improv_rx_position_ = 0;
        const size_t position = improv_rx_position_;
        improv_rx_buffer_[position] = byte;
        improv_last_byte_ms_ = millis();

        const bool frame_complete = position >= 9 && position == 9 + improv_rx_buffer_[8];
        if (frame_complete && improv_rx_buffer_[7] == improv::TYPE_RPC) {
            const size_t rpc_length = improv_rx_buffer_[8];
            const uint8_t* rpc = improv_rx_buffer_ + 9;
            bool structurally_valid = rpc_length >= 2 && rpc[1] == rpc_length - 2;
            if (structurally_valid && rpc[0] == improv::WIFI_SETTINGS) {
                structurally_valid = rpc_length >= 4;
                if (structurally_valid) {
                    const size_t password_length_index = 3 + rpc[2];
                    structurally_valid = password_length_index < rpc_length &&
                                         password_length_index + 1 + rpc[password_length_index] == rpc_length;
                }
            }
            if (!structurally_valid) {
                send_improv_error(improv::ERROR_INVALID_RPC);
                improv_rx_position_ = 0;
                continue;
            }
        }

        const bool keep = improv::parse_improv_serial_byte(
            position, byte, improv_rx_buffer_,
            [this](improv::ImprovCommand command) {
                return handle_improv_command(command);
            },
            [this](improv::Error error) {
                send_improv_error(error);
            });
        if (frame_complete || !keep) {
            improv_rx_position_ = 0;
            if (!keep && byte == 'I') {
                improv_rx_buffer_[0] = byte;
                improv_rx_position_ = 1;
            }
        } else {
            improv_rx_position_++;
        }
    }

    if (!improv_connect_pending_) return;
    if (network_manager.is_connected()) {
        complete_improv_connection();
    } else if (network_manager.state() == NetworkState::WIFI_SETUP_REQUIRED ||
               network_manager.state() == NetworkState::WIFI_DISABLED ||
               millis() - improv_connect_started_ms_ >= CONNECT_TIMEOUT_MS) {
        improv_connect_pending_ = false;
        send_improv_error(improv::ERROR_UNABLE_TO_CONNECT);
        send_improv_state(network_manager.is_enabled() ? improv::STATE_AUTHORIZED
                                                       : improv::STATE_STOPPED);
    }
}

bool ProvisioningService::handle_improv_command(const improv::ImprovCommand& command) {
    send_improv_error(improv::ERROR_NONE);
    switch (command.command) {
        case improv::WIFI_SETTINGS: {
            if (improv_connect_pending_ || !network_manager.is_enabled()) {
                send_improv_error(improv::ERROR_UNABLE_TO_CONNECT);
                return true;
            }
            const String ssid(command.ssid.c_str());
            const String password(command.password.c_str());
            if (!network_manager.set_credentials(ssid, password)) {
                send_improv_error(improv::ERROR_INVALID_RPC);
                return true;
            }
            stop_dns();
            if (network_manager.state() != NetworkState::WIFI_CONNECTING &&
                !network_manager.connect_saved_credentials()) {
                send_improv_error(improv::ERROR_UNABLE_TO_CONNECT);
                return true;
            }
            improv_connect_pending_ = true;
            improv_connect_started_ms_ = millis();
            send_improv_state(improv::STATE_PROVISIONING);
            return true;
        }
        case improv::GET_CURRENT_STATE: {
            const improv::State state = !network_manager.is_enabled()
                                            ? improv::STATE_STOPPED
                                            : network_manager.is_connected()
                                                  ? improv::STATE_PROVISIONED
                                                  : improv_connect_pending_
                                                        ? improv::STATE_PROVISIONING
                                                        : improv::STATE_AUTHORIZED;
            send_improv_state(state);
            if (state == improv::STATE_PROVISIONED) {
                send_improv_response(command.command,
                                     {"http://" + network_manager.ip_address()});
            }
            return true;
        }
        case improv::GET_DEVICE_INFO: {
#ifdef HW_DISPLAY_VARIANT_V2
            const String hardware = "Waveshare ESP32-S3 Touch AMOLED 1.64 V2";
#else
            const String hardware = "Waveshare ESP32-S3 Touch AMOLED 1.64 V1";
#endif
            send_improv_response(command.command,
                                 {"Smart Grind-by-Weight", BUILD_FIRMWARE_VERSION,
                                  hardware, "Smart Grind"});
            return true;
        }
        case improv::GET_NETWORK_STATE: {
            uint8_t flags = improv::NETWORK_SUPPORTS_WIFI;
            std::vector<String> values;
            if (network_manager.is_connected()) {
                flags |= improv::NETWORK_IS_ONLINE;
                values.push_back(String(flags));
                values.push_back("http://" + network_manager.ip_address());
            } else {
                values.push_back(String(flags));
            }
            send_improv_response(command.command, values);
            return true;
        }
        case improv::BAD_CHECKSUM:
        case improv::UNKNOWN:
            send_improv_error(improv::ERROR_INVALID_RPC);
            return false;
        default:
            send_improv_error(improv::ERROR_UNKNOWN_RPC);
            return false;
    }
}

void ProvisioningService::send_improv_state(improv::State state) {
    const uint8_t value = static_cast<uint8_t>(state);
    send_improv_packet(improv::TYPE_CURRENT_STATE, &value, 1);
}

void ProvisioningService::send_improv_error(improv::Error error) {
    const uint8_t value = static_cast<uint8_t>(error);
    send_improv_packet(improv::TYPE_ERROR_STATE, &value, 1);
}

void ProvisioningService::send_improv_response(improv::Command command,
                                               const std::vector<String>& values) {
    const std::vector<uint8_t> response = improv::build_rpc_response(command, values, false);
    send_improv_packet(improv::TYPE_RPC_RESPONSE, response.data(), response.size());
}

void ProvisioningService::send_improv_packet(improv::ImprovSerialType type,
                                             const uint8_t* data, size_t length) {
    if (length > 255 || (length > 0 && !data)) return;
    uint8_t frame[267] = {'I', 'M', 'P', 'R', 'O', 'V', improv::IMPROV_SERIAL_VERSION,
                          static_cast<uint8_t>(type), static_cast<uint8_t>(length)};
    if (length > 0) memcpy(frame + 9, data, length);
    uint8_t checksum = 0;
    for (size_t i = 0; i < 9 + length; ++i) checksum += frame[i];
    frame[9 + length] = checksum;
    frame[10 + length] = '\n';
    Serial.write(frame, 11 + length);
}

void ProvisioningService::complete_improv_connection() {
    improv_connect_pending_ = false;
    send_improv_state(improv::STATE_PROVISIONED);
    send_improv_response(improv::WIFI_SETTINGS,
                         {"http://" + network_manager.ip_address()});
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
