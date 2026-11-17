#pragma once

#include <cstdint>
#include "../config/constants.h"

/**
 * FileIOTask - Low-priority flash and filesystem work
 *
 * Runs on Core 1 at the lowest priority so LittleFS writes never block the
 * weight-sampling or grind-control loops. Each cycle drains GrindController's
 * queued flash operations (session start/end) and log messages, and every
 * 30 s it checks that the LittleFS partition is still mounted, remounting it
 * without formatting when it is not.
 *
 * TaskManager creates the FreeRTOS task and calls task_impl() from it.
 */
class FileIOTask {
private:
    uint32_t last_filesystem_check_time;

    // Performance monitoring
    uint32_t cycle_count;
    uint32_t cycle_time_sum_ms;
    uint32_t cycle_time_min_ms;
    uint32_t cycle_time_max_ms;
    uint32_t last_heartbeat_time;

public:
    FileIOTask();

    // Task body, run by the FreeRTOS task TaskManager creates
    void task_impl();

private:
    // File system management
    void check_filesystem_health();
    bool validate_filesystem_access();
    void handle_filesystem_error();
    bool attempt_filesystem_recovery();

    // Performance tracking
    void record_timing(uint32_t start_time, uint32_t end_time);
    void print_heartbeat() const;
    void reset_performance_metrics();
};

// Global instance
extern FileIOTask file_io_task;
