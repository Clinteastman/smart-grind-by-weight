#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <esp_event.h>
#include <cstdint>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "bluetooth/manager.h"
#include "config/build_info.h"
#include "config/constants.h"
#include "controllers/grind_controller.h"
#include "controllers/profile_controller.h"
#include "hardware/hardware_manager.h"
#include "network/device_web_server.h"
#include "network/gaggimate_status_client.h"
#include "network/network_manager.h"
#include "network/provisioning_service.h"
#include "storage/filesystem.h"
#include "storage/preferences.h"
#include "system/device_info.h"
#include "system/screensaver_settings.h"
#include "system/state_machine.h"
#include "system/statistics_manager.h"
#include "system/timing.h"
#include "tasks/grind_control_task.h"
#include "tasks/task_manager.h"
#include "tasks/weight_sampling_task.h"
#include "ui/ui_manager.h"

HardwareManager hardware_manager;
StateMachine state_machine;
ProfileController profile_controller;
GrindController grind_controller;
UIManager ui_manager;
BluetoothManager bluetooth_manager;

#if SYS_ENABLE_REALTIME_HEARTBEAT
// Core 1 timing metrics (global scope for main loop access)
static uint32_t core1_cycle_count_10s = 0;
static uint32_t core1_cycle_time_sum_ms = 0;
static uint32_t core1_cycle_time_min_ms = UINT32_MAX;
static uint32_t core1_cycle_time_max_ms = 0;
static uint32_t core1_last_heartbeat_time = 0;
#endif

namespace {

float load_startup_display_brightness() {
    Preferences prefs;
    float brightness = USER_SCREEN_BRIGHTNESS_NORMAL;
    if (prefs.begin("brightness", true)) {
        brightness = prefs.getFloat("normal", USER_SCREEN_BRIGHTNESS_NORMAL);
        prefs.end();
    }

    if (brightness < 0.15f) {
        brightness = 0.15f;
    }
    return brightness;
}

void draw_early_startup_splash_if_ready() {
    if (!state_machine.is_state(UIState::READY) ||
        !ScreensaverSettings::is_startup_enabled() ||
        !filesystem.exists(BLE_IMAGE_FILENAME)) {
        return;
    }

    DisplayManager* display = hardware_manager.get_display();
    if (!display || !display->is_initialized()) {
        return;
    }

    display->set_brightness(load_startup_display_brightness());

    if (display->draw_rgb565_file(BLE_IMAGE_FILENAME,
                                  HW_DISPLAY_WIDTH_PX,
                                  HW_DISPLAY_HEIGHT_PX)) {
        LOG_BLE("[STARTUP] Early screensaver splash drawn\n");
    }
}

/**
 * Route the console through the USB-Serial-JTAG driver.
 *
 * Everything in this firmware logs with printf, and Improv provisioning needs
 * non-blocking reads from that same port, which the register-level default
 * console cannot do. Installing the driver and pointing stdio at it keeps log
 * output and Improv frames in one ordered stream.
 */
void init_console() {
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    config.rx_buffer_size = 512;
    config.tx_buffer_size = 1024;
    const esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK) {
        // Logging still works through the register-level console; only Improv
        // serial provisioning is lost, and it checks for the driver itself.
        LOG_BLE("[STARTUP] Console driver unavailable (%s); Improv serial disabled\n",
                esp_err_to_name(err));
        return;
    }
    usb_serial_jtag_vfs_use_driver();
}

/**
 * Confirm the running image so the bootloader keeps it. After an update the
 * bootloader rolls back to the previous image at the next reset unless the new
 * one confirms itself, and grinders updated from the Arduino firmware keep its
 * rollback-enabled bootloader. Called once start-up has completed, so an image
 * that crashes or hangs before then is rolled back.
 */
void confirm_running_image() {
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    LOG_BLE("[STARTUP] Confirmed updated firmware: %s\n", esp_err_to_name(err));
}

/**
 * Bring up the settings partition and the shared event loop that the
 * Wi-Fi, BLE and HTTP services all attach to.
 */
void init_platform_services() {
    init_console();

    esp_err_t nvs_status = nvs_flash_init();
    if (nvs_status == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // A firmware update can leave the settings partition without free
        // pages. Reformatting loses stored settings but keeps the device
        // bootable, and every setting has a safe default.
        LOG_BLE("[STARTUP] NVS partition needs erasing (%s)\n", esp_err_to_name(nvs_status));
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_status = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_status);

    ESP_ERROR_CHECK(esp_netif_init());
    // Both the Wi-Fi netifs and the HTTP server attach to the default loop.
    // ESP_ERR_INVALID_STATE only means someone got here first, which is fine.
    const esp_err_t loop_status = esp_event_loop_create_default();
    if (loop_status != ESP_OK && loop_status != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(loop_status);
    }
}

/**
 * Name of a reset reason for the startup log. The switch has no default, so
 * -Wswitch fails the build when ESP-IDF adds a reason this list lacks.
 */
const char* reset_reason_name(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN: return "UNKNOWN";
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXT (Reset Pin)";
        case ESP_RST_SW: return "SW (esp_restart)";
        case ESP_RST_PANIC: return "PANIC (Exception)";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        case ESP_RST_USB: return "USB (Serial/JTAG port)";
        case ESP_RST_JTAG: return "JTAG";
        case ESP_RST_EFUSE: return "EFUSE";
        case ESP_RST_PWR_GLITCH: return "POWER_GLITCH";
        case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
    }
    return "UNKNOWN";
}

void setup() {
#ifdef UI_DEBUG_SERIAL_DELAY_MS
    vTaskDelay(pdMS_TO_TICKS(UI_DEBUG_SERIAL_DELAY_MS));
#endif
    
    // Log reset reason to help diagnose unexpected resets/freeze scenarios
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    LOG_BLE("[STARTUP] Reset reason: %s (%d)\n", reset_reason_name(reset_reason), reset_reason);
    
    
    // Early startup heartbeat - helps capture initialization sequence
    LOG_BLE("[STARTUP] Initializing ESP32-S3 Coffee Scale - Build %d - Core1 active\n", BUILD_NUMBER);
    
    // Initialize LittleFS once - format if necessary
    if (!filesystem.begin(true)) {
        LOG_BLE("ERROR: LittleFS mount failed - continuing without filesystem\n");
    } else {
        LOG_BLE("✅ LittleFS mounted successfully\n");
    }
    
    hardware_manager.init();
    profile_controller.init(hardware_manager.get_preferences());
    statistics_manager.init(hardware_manager.get_preferences());
    grind_controller.init(hardware_manager.get_load_cell(), hardware_manager.get_grinder(), hardware_manager.get_preferences());
    
    // Set up the reference so HardwareManager can query GrindController state
    hardware_manager.set_grind_controller(&grind_controller);

    // Wi-Fi and HTTP services start asynchronously so they never block the
    // real-time scale, motor, touch, or rendering tasks.
    network_manager.init(hardware_manager.get_preferences());
    gaggimate_status_client.init();
    device_web_server.init(&hardware_manager, &grind_controller, &bluetooth_manager, &profile_controller);
    provisioning_service.init(hardware_manager.get_preferences());
    
    bluetooth_manager.init(hardware_manager.get_preferences());
    
    // Check for OTA failure to determine initial state
    const std::string failed_ota_build = bluetooth_manager.check_ota_failure_after_boot();
    const bool ota_failed = !failed_ota_build.empty();

    // Check calibration status to determine initial screen
    bool is_calibrated = hardware_manager.get_weight_sensor()->is_calibrated();
    GrindMode boot_grind_mode = profile_controller.get_grind_mode();

    if (ota_failed) {
        LOG_BLE("BOOT: Starting in OTA failure state for expected build %s\n", failed_ota_build.c_str());
        state_machine.init(UIState::OTA_UPDATE_FAILED);
    } else if (!is_calibrated && boot_grind_mode != GrindMode::TIME) {
        LOG_BLE("BOOT: Device not calibrated - starting in CALIBRATION state\n");
        state_machine.init(UIState::CALIBRATION);
    } else {
        state_machine.init(UIState::READY);
    }

    draw_early_startup_splash_if_ready();
    
    ui_manager.init(&hardware_manager, &state_machine, &profile_controller, &grind_controller, &bluetooth_manager);
    
    // Store OTA failure info in ui_manager if needed
    if (ota_failed) {
        if (auto* ota = ui_manager.get_ota_data_export_controller()) {
            ota->set_failure_info(failed_ota_build.c_str());
        }
    }
    
    // Enable BLE by default during bootup with 2-minute timeout
    // (Previously disabled by default for security, now enabled for user convenience)
    bluetooth_manager.enable_during_bootup();
    
    // Initialize individual task modules BEFORE TaskManager creates FreeRTOS tasks
    // This ensures all task dependencies are ready before tasks start running
    LOG_BLE("[STARTUP] Initializing task module dependencies...\n");
    weight_sampling_task.init(hardware_manager.get_load_cell());
    grind_control_task.init(&grind_controller, hardware_manager.get_load_cell(),
                           hardware_manager.get_grinder());
    
    LOG_BLE("✅ Task module dependencies initialized\n");
    
    // Initialize TaskManager with hardware and system interfaces
    LOG_BLE("[STARTUP] Initializing FreeRTOS Task Architecture...\n");
    bool task_init_success = task_manager.init(&hardware_manager, &state_machine, &profile_controller, 
                                              &grind_controller, &bluetooth_manager, &ui_manager);
    
    if (!task_init_success) {
        LOG_BLE("ERROR: Failed to initialize TaskManager - system cannot start\n");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000)); // Halt system if task initialization fails
        }
    }
    
    LOG_BLE("✅ TaskManager initialized successfully\n");
    confirm_running_image();
}

void loop() {


#if SYS_ENABLE_REALTIME_HEARTBEAT
    // Core 1 main loop timing (monitor main loop health)
    uint32_t cycle_start_time = millis();
    core1_cycle_count_10s++;
    if (core1_last_heartbeat_time == 0) core1_last_heartbeat_time = cycle_start_time;
#endif

    // Update device uptime statistics every 15 minutes, reporting back in hours
    static uint32_t last_uptime_update = 0;
    static uint32_t pending_uptime_minutes = 0;
    uint32_t current_time = millis();
    network_manager.update();
    provisioning_service.update();
    // The HTTP listener needs lwIP, which only exists once the Wi-Fi driver is
    // running in station or access-point mode, so defer it until
    // NetworkManager/ProvisioningService has brought the radio up.
    device_web_server.begin();
    device_web_server.update();
    if (last_uptime_update == 0) {
        last_uptime_update = current_time;
    }
    constexpr uint32_t kUptimeIntervalMs = 900000; // 15 minutes
    constexpr uint32_t kUptimeIntervalMinutes = 15;
    uint32_t elapsed_ms = current_time - last_uptime_update;
    if (elapsed_ms >= kUptimeIntervalMs) {
        uint32_t intervals = elapsed_ms / kUptimeIntervalMs;
        pending_uptime_minutes += intervals * kUptimeIntervalMinutes;
        last_uptime_update += intervals * kUptimeIntervalMs;

        if (pending_uptime_minutes > 0) {
            statistics_manager.update_uptime(pending_uptime_minutes);
            pending_uptime_minutes = 0;
        }
    }
    
    // Check OTA state and suspend hardware tasks if needed
    static bool hardware_suspended = false;
    bool ota_active = bluetooth_manager.is_updating() || device_web_server.is_ota_active();
    
    if (ota_active && !hardware_suspended) {
        task_manager.suspend_hardware_tasks();
        hardware_suspended = true;
        LOG_BLE("[MAIN] Hardware tasks suspended for OTA\n");
    } else if (!ota_active && hardware_suspended) {
        task_manager.resume_hardware_tasks();
        hardware_suspended = false;
        LOG_BLE("[MAIN] Hardware tasks resumed after OTA\n");
    }
    
    // UI events are now processed inside the UI render FreeRTOS task
    // to serialize all LVGL updates on a single thread.
    
#if SYS_ENABLE_REALTIME_HEARTBEAT
    // Calculate Core 1 main loop timing
    uint32_t cycle_end_time = millis();
    uint32_t cycle_duration = cycle_end_time - cycle_start_time;
    core1_cycle_time_sum_ms += cycle_duration;
    if (cycle_duration < core1_cycle_time_min_ms) core1_cycle_time_min_ms = cycle_duration;
    if (cycle_duration > core1_cycle_time_max_ms) core1_cycle_time_max_ms = cycle_duration;
    
    // Core 1 Main Loop Heartbeat - Monitor main loop health (every 10 seconds)
    if (cycle_end_time - core1_last_heartbeat_time >= SYS_REALTIME_HEARTBEAT_INTERVAL_MS) {
        uint32_t avg_cycle_time = core1_cycle_count_10s > 0 ? core1_cycle_time_sum_ms / core1_cycle_count_10s : 0;
        
        // Get system states
        bool is_grinding = grind_controller.is_active();
        const char* ble_state = bluetooth_manager.is_enabled() ? 
                               (bluetooth_manager.is_connected() ? "CONN" : "ADV") : "OFF";
        const char* grinder_state = is_grinding ? "ACTIVE" : "IDLE";
        const char* tasks_status = task_manager.are_tasks_healthy() ? "HEALTHY" : "ERROR";
        size_t free_heap_kb = device_info::free_heap_bytes() / 1024;
        
        LOG_BLE("[%lums MAIN_LOOP_HEARTBEAT] Cycles: %lu/10s | Avg: %lums (%lu-%lums) | Tasks: %s | BLE: %s | Grinder: %s | Mem: %zuKB | Build: #%d\n",
               millis(), core1_cycle_count_10s, avg_cycle_time, core1_cycle_time_min_ms, core1_cycle_time_max_ms,
               tasks_status, ble_state, grinder_state, free_heap_kb, BUILD_NUMBER);
        
        // Reset Core 1 metrics for next interval
        core1_cycle_count_10s = 0;
        core1_cycle_time_sum_ms = 0;
        core1_cycle_time_min_ms = UINT32_MAX;
        core1_cycle_time_max_ms = 0;
        core1_last_heartbeat_time = cycle_end_time;
    }
#endif
    
    // The main loop now runs much lighter since FreeRTOS tasks handle all the heavy work
    // Just yield to allow FreeRTOS scheduler to run other tasks efficiently
    vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to prevent starving other tasks
}

}  // namespace

extern "C" void app_main() {
    init_platform_services();
    setup();
    // The service loop keeps running in the main task, which sdkconfig pins to
    // core 1 so the Wi-Fi and BLE stacks keep core 0 to themselves.
    while (true) {
        loop();
    }
}
