#pragma once

#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>

#include "../storage/preferences.h"

#include "../config/constants.h"
#include "ota_handler.h"
#include "data_stream.h"
#include "image_upload_handler.h"

// Forward declaration to avoid circular dependency
class UIManager;

// Small message used to marshal BLE UI status updates to the UI task
struct UIStatusMessage {
    char text[64];
};

// Debug command enums
enum BLEDebugCommand {
    BLE_DEBUG_CMD_ENABLE = 0x01,
    BLE_DEBUG_CMD_DISABLE = 0x02
};

// Data export enums
enum BLEDataCommand {
    BLE_DATA_CMD_STOP_EXPORT = 0x11,
    BLE_DATA_CMD_GET_COUNT = 0x12,
    BLE_DATA_CMD_CLEAR_DATA = 0x13,
    BLE_DATA_CMD_GET_FILE_LIST = 0x14,
    BLE_DATA_CMD_REQUEST_FILE = 0x15
};

enum BLEDataStatus {
    BLE_DATA_IDLE = 0x20,
    BLE_DATA_EXPORTING = 0x21,
    BLE_DATA_COMPLETE = 0x22,
    BLE_DATA_ERROR = 0x23
};

// Screensaver timing settings commands/statuses, multiplexed on the Data service
enum BLEScreensaverSettingsCommand {
    BLE_SETTINGS_CMD_GET_SCREENSAVER = 0x40,
    BLE_SETTINGS_CMD_SET_SCREENSAVER = 0x41
};

enum BLEScreensaverSettingsStatus {
    BLE_SETTINGS_STATUS_VALUE = 0x42,
    BLE_SETTINGS_STATUS_ERROR = 0x44
};

enum BLEScreensaverSettingsError {
    BLE_SETTINGS_ERROR_BUSY = 0x01,
    BLE_SETTINGS_ERROR_INVALID_LENGTH = 0x02,
    BLE_SETTINGS_ERROR_INVALID_RANGE = 0x03,
    BLE_SETTINGS_ERROR_STORAGE = 0x04
};

/**
 * BluetoothManager - Central BLE communication manager
 * 
 * Handles BLE connection, characteristic management, and coordinates
 * between OTA updates and data export operations.
 */
class BluetoothManager : public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
private:
    // BLE Server and services
    NimBLEServer* ble_server;
    NimBLEService* ota_service;
    NimBLEService* data_service;
    NimBLEService* debug_service;
    NimBLEService* sysinfo_service;
    
    // OTA characteristics
    NimBLECharacteristic* ota_data_characteristic;
    NimBLECharacteristic* ota_control_characteristic;
    NimBLECharacteristic* ota_status_characteristic;
    NimBLECharacteristic* build_number_characteristic;
    
    // Data export characteristics
    NimBLECharacteristic* data_control_characteristic;
    NimBLECharacteristic* data_transfer_characteristic;
    NimBLECharacteristic* data_status_characteristic;
    
    // Debug characteristics
    NimBLECharacteristic* debug_rx_characteristic;
    NimBLECharacteristic* debug_tx_characteristic;
    
    // System info characteristics
    NimBLECharacteristic* sysinfo_system_characteristic;
    NimBLECharacteristic* sysinfo_performance_characteristic;
    NimBLECharacteristic* sysinfo_hardware_characteristic;
    NimBLECharacteristic* sysinfo_sessions_characteristic;
    NimBLECharacteristic* sysinfo_diagnostics_characteristic;
    
    // Connection state
    bool device_connected;
    uint16_t conn_handle;  // the connected client's handle, BLE_HS_CONN_HANDLE_NONE when idle
    bool ble_enabled;
    bool debug_stream_active;
    unsigned long enable_time;
    unsigned long timeout_ms;
    unsigned long last_disconnect_time;
    
    // Component handlers
    OTAHandler ota_handler;
    DataStreamManager data_stream;
    ImageUploadHandler image_handler;
    
    // Data export state
    bool data_export_in_progress;
    BLEDataStatus data_status;
    uint16_t current_chunk;
    unsigned long next_chunk_time;
    // A chunk read from the session file but not yet accepted by the stack.
    // It is sent again, unchanged, until a notification buffer is free.
    uint8_t pending_chunk[BLE_DATA_CHUNK_SIZE_BYTES];
    size_t pending_chunk_size;
    uint16_t notify_retries;
    uint32_t current_file_session_id;  // For per-file streaming
    bool sessions_info_dirty;
    uint32_t last_session_storage_version;
    bool last_reported_export_state;
    
    // Queue to marshal UI status messages to UI task context
    QueueHandle_t ui_status_queue;

    // Diagnostics report control flags
    bool diagnostic_report_pending;
    bool diagnostic_report_in_progress;

    // disable() frees every service and characteristic. The UI and service
    // loop call enable()/disable() while the Bluetooth task runs handle(), so
    // all three hold this lock. NimBLE callbacks must not take it: deinit waits
    // for the NimBLE host task while holding it.
    std::recursive_mutex lifecycle_mutex;

    // Private methods
    void update_ui_status(const char* status);
    void enqueue_ui_status(const char* status);
    void set_ota_status(BLEOTAStatus status);
    void set_data_status(BLEDataStatus status);
    void handle_ota_control_command(NimBLECharacteristic* characteristic);
    void handle_ota_data_chunk(NimBLECharacteristic* characteristic);
    void handle_debug_command(NimBLECharacteristic* characteristic);
    void handle_data_control_command(NimBLECharacteristic* characteristic);
    void handle_image_control_command(uint8_t command, const std::string& value);
    void set_image_status(BLEImageStatus status);
    void handle_screensaver_settings_command(uint8_t command, const std::string& value);
    void send_screensaver_settings();
    void send_screensaver_settings_error(BLEScreensaverSettingsError error);
    bool is_data_channel_busy_for_settings() const;
    void send_next_data_chunk();
    void send_measurement_count();
    void send_log_message(const char* message);
    void clear_measurement_data();
    void send_file_list();
    void send_individual_file(uint32_t session_id);
    size_t data_chunk_capacity() const;
    void tune_link(NimBLEServer* server, const NimBLEConnInfo& conn_info);
    void log_link(const char* event, const NimBLEConnInfo& conn_info);
    void update_system_info();
    void update_performance_info();
    void update_hardware_info();
    bool update_sessions_info();
    void process_sessions_info_updates();
    void mark_sessions_info_dirty();
    void generate_diagnostic_report();
    
public:
    BluetoothManager();
    ~BluetoothManager();
    
    /**
     * Initialize Bluetooth manager
     */
    void init(Preferences* prefs);
    
    /**
     * Enable BLE and start advertising
     * @param timeout_ms Optional timeout in milliseconds (defaults to BLE_AUTO_DISABLE_TIMEOUT_MS)
     */
    void enable(unsigned long timeout_ms = 0);
    
    /**
     * Enable BLE during bootup with shorter timeout
     */
    void enable_during_bootup();
    
    /**
     * Disable BLE and cleanup
     */
    void disable();
    
    /**
     * Handle periodic updates (call from main loop)
     */
    void handle();
    
    /**
     * Start/stop advertising
     */
    void start_advertising();
    void stop_advertising();
    
    /**
     * Status queries
     */
    bool is_enabled() const { return ble_enabled; }
    bool is_connected() const { return device_connected; }
    bool is_updating() const { return ota_handler.is_ota_active(); }
    bool is_transfer_active() const {
        return ota_handler.is_ota_active() || data_export_in_progress ||
               image_handler.is_upload_active();
    }
    bool is_debug_stream_active() const { return debug_stream_active; }
    bool has_screensaver_image() const { return image_handler.has_image(); }
    bool begin_screensaver_image_upload(uint32_t size) { return image_handler.start_upload(size); }
    bool write_screensaver_image_chunk(const uint8_t* data, size_t size) {
        return image_handler.process_chunk(data, size);
    }
    bool finish_screensaver_image_upload() { return image_handler.complete_upload(); }
    void abort_screensaver_image_upload() { image_handler.abort_upload(); }
    bool delete_screensaver_image() { return image_handler.delete_image(); }
    
    /**
     * OTA progress information
     */
    float get_ota_progress() const { return ota_handler.get_progress(); }
    unsigned long get_remaining_time_ms() const;
    
    /**
     * General Bluetooth timeout information
     */
    unsigned long get_bluetooth_timeout_remaining_ms() const;
    
    /**
     * Data export methods
     */
    void start_data_export();
    void stop_data_export();
    void update_data_export();
    bool is_data_export_active() const { return data_export_in_progress; }
    float get_data_export_progress() const;
    uint32_t get_data_export_session_count() const;

    /**
     * Log a message to Serial and optionally over BLE
     */
    void log(const char* format, ...);
    
    /**
     * Update all system information characteristics
     */
    void refresh_system_info();
    
    /**
     * Check if OTA failed after reboot and return expected build number if so
     * @return Expected build number if OTA failed, empty string if no failure
     */
    std::string check_ota_failure_after_boot();
    
    // NimBLE callbacks
    void onConnect(NimBLEServer* server, NimBLEConnInfo& conn_info) override;
    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& conn_info, int reason) override;
    void onMTUChange(uint16_t mtu, NimBLEConnInfo& conn_info) override;
    void onConnParamsUpdate(NimBLEConnInfo& conn_info) override;
    void onPhyUpdate(NimBLEConnInfo& conn_info, uint8_t tx_phy, uint8_t rx_phy) override;
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& conn_info) override;

    // Drain a status message queued from BLE task; called by UI task
    bool dequeue_ui_status(char* out, size_t out_len);
};
