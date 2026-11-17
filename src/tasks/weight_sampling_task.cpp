#include "weight_sampling_task.h"
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../system/timing.h"
#include "../config/build_info.h"
#include "../hardware/WeightSensor.h"
#include "../config/constants.h"
#include <esp_task_wdt.h>

// Global instance
WeightSamplingTask weight_sampling_task;

WeightSamplingTask::WeightSamplingTask() {
    weight_sensor = nullptr;

    // Initialize performance metrics
    cycle_count = 0;
    cycle_time_sum_ms = 0;
    cycle_time_min_ms = UINT32_MAX;
    cycle_time_max_ms = 0;
    last_heartbeat_time = 0;
}

void WeightSamplingTask::init(WeightSensor* ws) {
    weight_sensor = ws;

    LOG_BLE("WeightSamplingTask: Initialized with hardware interfaces\n");
}

void WeightSamplingTask::task_impl() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SYS_TASK_WEIGHT_SAMPLING_INTERVAL_MS);

    LOG_BLE("WeightSamplingTask started on Core %d at %dHz\n",
            xPortGetCoreID(), 1000 / SYS_TASK_WEIGHT_SAMPLING_INTERVAL_MS);

    // Initialize HX711 hardware on Core 0 before starting sampling loop
    if (!initialize_hx711_hardware()) {
        LOG_BLE("ERROR: Failed to initialize HX711 hardware on Core 0\n");
        return;
    }

    LOG_BLE("WeightSamplingTask: Hardware initialization complete, starting sampling loop\n");

    // Add current task to watchdog monitoring
    esp_task_wdt_add(nullptr);

    // Reset performance metrics
    reset_performance_metrics();

    // Main sampling loop
    while (true) {
        uint32_t cycle_start_time = millis();

        // Core sampling operations
        sample_and_feed_weight_sensor();

        // Weight sensor state management (non-blocking operations only)
        if (weight_sensor) {
            weight_sensor->update();  // Coordinate tare state management
        }

        // Feed watchdog to prevent timeout
        esp_task_wdt_reset();

        uint32_t cycle_end_time = millis();

        // Record performance metrics
        record_timing(cycle_start_time, cycle_end_time);

        // Use vTaskDelayUntil for predictable timing (eliminates busy-wait)
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

bool WeightSamplingTask::initialize_hx711_hardware() {
    if (!weight_sensor) {
        LOG_BLE("ERROR: WeightSensor not available for hardware initialization\n");
        return false;
    }

    LOG_BLE("WeightSamplingTask: Initializing WeightSensor hardware on Core 0...\n");

    // Hardware reset sequence
    weight_sensor->power_down();
    vTaskDelay(pdMS_TO_TICKS(1000));
    weight_sensor->power_up();
    vTaskDelay(pdMS_TO_TICKS(500));

    bool begin_success = weight_sensor->begin();
    if (!begin_success) {
        LOG_BLE("ERROR: HX711 begin() failed - sensor not responding\n");
        weight_sensor->set_hardware_fault(WeightSensor::HardwareFault::NOT_CONNECTED);
        weight_sensor->power_down();
        return false;
    }
    weight_sensor->set_hardware_fault(WeightSensor::HardwareFault::NONE);

    // Apply saved calibration factor
    float saved_cal_factor = weight_sensor->get_saved_calibration_factor();
    weight_sensor->set_calibration_factor(saved_cal_factor);

    // Hardware stabilization - wait for hardware to be ready
    LOG_BLE("  Waiting for WeightSensor hardware stabilization...\n");
    uint32_t start_time = millis();
    while (millis() - start_time < 2000) {
        if (weight_sensor->data_waiting_async()) {
            weight_sensor->update_async();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Validate hardware responds
    if (!weight_sensor->validate_hardware()) {
        LOG_BLE("ERROR: WeightSensor hardware validation failed - check wiring!\n");
        if (weight_sensor->get_hardware_fault() == WeightSensor::HardwareFault::NONE) {
            weight_sensor->set_hardware_fault(WeightSensor::HardwareFault::NO_DATA);
        }
        weight_sensor->power_down();
        return false;
    }

    weight_sensor->set_hardware_fault(WeightSensor::HardwareFault::NONE);

    // Verify initialization
    LOG_BLE("  WeightSensor initialization complete:\n");
    LOG_BLE("    Calibration factor: %.2f\n", weight_sensor->get_calibration_factor());
    LOG_BLE("    Tare offset: %ld\n", weight_sensor->get_zero_offset());
    LOG_BLE("    Hardware ready: %s\n", weight_sensor->is_data_ready() ? "TRUE" : "FALSE");
    LOG_BLE("    Sample rate detected: %.1f SPS\n", weight_sensor->get_detected_sample_rate_sps());

    // Mark WeightSensor as hardware-ready
    weight_sensor->set_hardware_initialized();
    // Calibration restores the grams-per-count factor, but the zero offset is
    // intentionally established fresh on each boot to account for load-cell
    // drift and the assembled grinder's static preload.
    weight_sensor->tareNoDelay();
    LOG_BLE("[STARTUP] Automatic load-cell tare requested\n");

    // Attempt single verification reading (optional since validation already succeeded)
    if (weight_sensor->update_async()) {
        float test_reading = weight_sensor->get_instant_weight();
        LOG_BLE("    Verification reading: %.3fg\n", test_reading);
    } else {
        LOG_BLE("    Verification reading: No sample ready yet (normal for 10 SPS after validation)\n");
    }

    LOG_BLE("✅ WeightSensor hardware initialization successful on Core 0\n");
    return true;
}

void WeightSamplingTask::sample_and_feed_weight_sensor() {
    if (!weight_sensor) return;

    // Call WeightSensor's Core 0 sampling method - this performs HX711 sampling
    // and feeds data to CircularBufferMath, updating all weight readings
    bool sample_taken = weight_sensor->sample_and_feed_filter();

#if SYS_ENABLE_REALTIME_HEARTBEAT
    // Record timestamp for SPS tracking when a sample was actually taken
    if (sample_taken) {
        weight_sensor->record_sample_timestamp();
    }
#endif
}

bool WeightSamplingTask::validate_hardware_ready() const {
    bool weight_sensor_ready = (weight_sensor != nullptr && weight_sensor->is_initialized());

    LOG_BLE("WeightSamplingTask hardware validation:\n");
    LOG_BLE("  weight_sensor != nullptr: %s\n", (weight_sensor != nullptr) ? "YES" : "NO");
    LOG_BLE("  weight_sensor initialized: %s\n", weight_sensor_ready ? "YES" : "NO");

    return weight_sensor_ready;
}

void WeightSamplingTask::record_timing(uint32_t start_time, uint32_t end_time) {
    uint32_t cycle_duration = end_time - start_time;

    cycle_count++;
    cycle_time_sum_ms += cycle_duration;

    if (cycle_duration < cycle_time_min_ms) {
        cycle_time_min_ms = cycle_duration;
    }
    if (cycle_duration > cycle_time_max_ms) {
        cycle_time_max_ms = cycle_duration;
    }

#if SYS_ENABLE_REALTIME_HEARTBEAT
    // Print task heartbeat every 10 seconds
    if (last_heartbeat_time == 0) {
        last_heartbeat_time = start_time;
    }

    if (end_time - last_heartbeat_time >= SYS_REALTIME_HEARTBEAT_INTERVAL_MS) {
        print_heartbeat();
        reset_performance_metrics();
        last_heartbeat_time = end_time;
    }
#endif
}

void WeightSamplingTask::print_heartbeat() const {
#if SYS_ENABLE_REALTIME_HEARTBEAT
    uint32_t avg_cycle_time = cycle_count > 0 ? cycle_time_sum_ms / cycle_count : 0;
    float current_sps = weight_sensor ? weight_sensor->get_current_sps() : 0.0f;
    int current_sample_count = weight_sensor ? weight_sensor->get_sample_count() : 0;
    int32_t raw_reading = weight_sensor ? weight_sensor->get_raw_adc_instant() : 0;

    LOG_BLE("[%lums WEIGHT_SAMPLING_HEARTBEAT] Cycles: %lu/10s | Avg: %lums (%lu-%lums) | Weight: %.3fg | Raw: %ld | SPS: %.1f | Samples: %d | Build: #%d\n",
           millis(), cycle_count, avg_cycle_time, cycle_time_min_ms, cycle_time_max_ms,
           weight_sensor ? weight_sensor->get_weight_low_latency() : 0.0f,
           (long)raw_reading, current_sps, current_sample_count, BUILD_NUMBER);
#endif
}

void WeightSamplingTask::reset_performance_metrics() {
    cycle_count = 0;
    cycle_time_sum_ms = 0;
    cycle_time_min_ms = UINT32_MAX;
    cycle_time_max_ms = 0;
}
