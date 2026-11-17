#include "grind_control_task.h"
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../system/timing.h"
#include "../config/build_info.h"
#include "../controllers/grind_controller.h"
#include "../hardware/WeightSensor.h"
#include "../hardware/grinder.h"
#include "../config/constants.h"
#include <esp_task_wdt.h>

// Global instance
GrindControlTask grind_control_task;

GrindControlTask::GrindControlTask() {
    grind_controller = nullptr;
    weight_sensor = nullptr;
    grinder = nullptr;

    // Initialize performance metrics
    cycle_count = 0;
    cycle_time_sum_ms = 0;
    cycle_time_min_ms = UINT32_MAX;
    cycle_time_max_ms = 0;
    last_heartbeat_time = 0;

    // Initialize grind state
    grind_active = false;
    grind_start_time = 0;
}

void GrindControlTask::init(GrindController* gc, WeightSensor* ws, Grinder* gr) {
    grind_controller = gc;
    weight_sensor = ws;
    grinder = gr;

    LOG_BLE("GrindControlTask: Initialized with hardware interfaces\n");
}

void GrindControlTask::task_impl() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SYS_TASK_GRIND_CONTROL_INTERVAL_MS);

    LOG_BLE("GrindControlTask started on Core %d at %dHz\n",
            xPortGetCoreID(), 1000 / SYS_TASK_GRIND_CONTROL_INTERVAL_MS);

    // Add current task to watchdog monitoring
    esp_task_wdt_add(nullptr);

    // Reset performance metrics
    reset_performance_metrics();

    // Main grind control loop
    while (true) {
        uint32_t cycle_start_time = millis();

        // Update grind control logic
        update_grind_control();

        // Monitor grind state changes
        monitor_grind_state();

        // Feed watchdog to prevent timeout
        esp_task_wdt_reset();

        uint32_t cycle_end_time = millis();

        // Record performance metrics
        record_timing(cycle_start_time, cycle_end_time);

        // Use vTaskDelayUntil for predictable timing (eliminates busy-wait)
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void GrindControlTask::update_grind_control() {
    if (!grind_controller) return;

    // Execute main grind controller logic
    // This calls the existing GrindController::update() method which contains
    // all the grinding algorithms, state machine, and pulse control logic
    grind_controller->update();
}

void GrindControlTask::monitor_grind_state() {
    if (!grind_controller) return;

    bool current_grind_active = grind_controller->is_active();

    // Detect grind start
    if (!grind_active && current_grind_active) {
        grind_active = true;
        grind_start_time = millis();
        LOG_BLE("GrindControlTask: Grind session started\n");
    }
    // Detect grind end
    else if (grind_active && !current_grind_active) {
        grind_active = false;
        uint32_t grind_duration = millis() - grind_start_time;
        LOG_BLE("GrindControlTask: Grind session ended (duration: %lums)\n", grind_duration);
    }
}

bool GrindControlTask::validate_hardware_ready() const {
    bool grind_controller_ready = (grind_controller != nullptr);
    bool weight_sensor_ready = (weight_sensor != nullptr && weight_sensor->is_initialized());
    bool grinder_ready = (grinder != nullptr && grinder->is_initialized());

    LOG_BLE("GrindControlTask hardware validation:\n");
    LOG_BLE("  grind_controller != nullptr: %s\n", grind_controller_ready ? "YES" : "NO");
    LOG_BLE("  weight_sensor ready: %s\n", weight_sensor_ready ? "YES" : "NO");
    LOG_BLE("  grinder ready: %s\n", grinder_ready ? "YES" : "NO");

    return grind_controller_ready && weight_sensor_ready && grinder_ready;
}

void GrindControlTask::record_timing(uint32_t start_time, uint32_t end_time) {
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

void GrindControlTask::print_heartbeat() const {
#if SYS_ENABLE_REALTIME_HEARTBEAT
    uint32_t avg_cycle_time = cycle_count > 0 ? cycle_time_sum_ms / cycle_count : 0;
    float target_weight = grind_controller ? grind_controller->get_target_weight() : 0.0f;
    float current_weight = weight_sensor ? weight_sensor->get_weight_low_latency() : 0.0f;
    const char* grind_status = grind_active ? "ACTIVE" : "IDLE";

    LOG_BLE("[%lums GRIND_CONTROL_HEARTBEAT] Cycles: %lu/10s | Avg: %lums (%lu-%lums) | Status: %s | Target: %.1fg | Current: %.3fg | Build: #%d\n",
           millis(), cycle_count, avg_cycle_time, cycle_time_min_ms, cycle_time_max_ms,
           grind_status, target_weight, current_weight, BUILD_NUMBER);
#endif
}

void GrindControlTask::reset_performance_metrics() {
    cycle_count = 0;
    cycle_time_sum_ms = 0;
    cycle_time_min_ms = UINT32_MAX;
    cycle_time_max_ms = 0;
}
