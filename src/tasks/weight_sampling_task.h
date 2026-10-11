#pragma once

#include <cstdint>
#include "../config/constants.h"

// Forward declarations
class WeightSensor;

/**
 * WeightSamplingTask - Load cell sampling loop
 *
 * Runs on Core 0 at the highest task priority. On start it brings up the
 * HX711 on that core (power cycle, calibration factor, hardware validation,
 * boot tare), then polls the sensor at a fixed rate, feeding each new sample
 * into the WeightSensor filters and driving its tare state machine. Feeds the
 * task watchdog each cycle and does no file I/O or other blocking work.
 *
 * TaskManager creates the FreeRTOS task and calls task_impl() from it.
 */
class WeightSamplingTask {
private:
    // Hardware interface
    WeightSensor* weight_sensor;

    // Performance monitoring
    uint32_t cycle_count;
    uint32_t cycle_time_sum_ms;
    uint32_t cycle_time_min_ms;
    uint32_t cycle_time_max_ms;
    uint32_t last_heartbeat_time;

public:
    WeightSamplingTask();

    // Initialization
    void init(WeightSensor* ws);

    // Task body, run by the FreeRTOS task TaskManager creates
    void task_impl();

    // Hardware validation (public for TaskManager access)
    bool validate_hardware_ready() const;

private:
    // Hardware management
    bool initialize_hx711_hardware();
    void sample_and_feed_weight_sensor();

    // Performance tracking
    void record_timing(uint32_t start_time, uint32_t end_time);
    void print_heartbeat() const;
    void reset_performance_metrics();
};

// Global instance
extern WeightSamplingTask weight_sampling_task;
