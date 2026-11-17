#pragma once

#include <cstdint>
#include "../config/constants.h"

// Forward declarations
class GrindController;
class WeightSensor;
class Grinder;

/**
 * GrindControlTask - Grind control loop
 *
 * Runs GrindController::update() at 50 Hz on Core 0, one priority level below
 * weight sampling, so the predictive and pulse algorithms act on fresh samples
 * and never wait on file I/O or rendering. Logs when a grind session starts
 * and ends, and feeds the task watchdog each cycle.
 *
 * TaskManager creates the FreeRTOS task and calls task_impl() from it.
 */
class GrindControlTask {
private:
    // Hardware and controller interfaces
    GrindController* grind_controller;
    WeightSensor* weight_sensor;
    Grinder* grinder;

    // Performance monitoring
    uint32_t cycle_count;
    uint32_t cycle_time_sum_ms;
    uint32_t cycle_time_min_ms;
    uint32_t cycle_time_max_ms;
    uint32_t last_heartbeat_time;

    // Grind session tracking for the log
    bool grind_active;
    uint32_t grind_start_time;

public:
    GrindControlTask();

    // Initialization
    void init(GrindController* gc, WeightSensor* ws, Grinder* gr);

    // Task body, run by the FreeRTOS task TaskManager creates
    void task_impl();

    // Hardware validation (public for TaskManager access)
    bool validate_hardware_ready() const;

private:
    // Grind control coordination
    void update_grind_control();
    void monitor_grind_state();

    // Performance tracking
    void record_timing(uint32_t start_time, uint32_t end_time);
    void print_heartbeat() const;
    void reset_performance_metrics();
};

// Global instance
extern GrindControlTask grind_control_task;
