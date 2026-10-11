#include "file_io_task.h"
#include <freertos/FreeRTOS.h>
#include <cstdint>
#include <freertos/task.h>
#include "../system/timing.h"
#include "../config/build_info.h"
#include "../controllers/grind_controller.h"
#include "../config/constants.h"
#include "../storage/filesystem.h"

// Global instance
FileIOTask file_io_task;

FileIOTask::FileIOTask() {
    last_filesystem_check_time = 0;

    // Initialize performance metrics
    cycle_count = 0;
    cycle_time_sum_ms = 0;
    cycle_time_min_ms = UINT32_MAX;
    cycle_time_max_ms = 0;
    last_heartbeat_time = 0;
}

void FileIOTask::task_impl() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SYS_TASK_FILE_IO_INTERVAL_MS);

    LOG_BLE("FileIOTask started on Core %d at %dHz\n",
            xPortGetCoreID(), 1000 / SYS_TASK_FILE_IO_INTERVAL_MS);

    // Reset performance metrics
    reset_performance_metrics();

    // Main file I/O processing loop
    while (true) {
        uint32_t cycle_start_time = millis();

        // Drain the GrindController queues here so flash operations
        // (start/end session) run on Core 1 in this low-priority task
        extern GrindController grind_controller;
        grind_controller.process_queued_flash_operations();
        grind_controller.process_queued_log_messages();

        // Periodic filesystem health check
        if (cycle_start_time - last_filesystem_check_time >= 30000) { // Every 30 seconds
            check_filesystem_health();
            last_filesystem_check_time = cycle_start_time;
        }

        uint32_t cycle_end_time = millis();

        // Record performance metrics
        record_timing(cycle_start_time, cycle_end_time);

        // Use vTaskDelayUntil for predictable timing (eliminates busy-wait)
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void FileIOTask::check_filesystem_health() {
    // An unavailable filesystem is mounted again on every check until that
    // succeeds, not only when it first becomes unavailable.
    if (!validate_filesystem_access()) {
        handle_filesystem_error();
    }
}

bool FileIOTask::validate_filesystem_access() {
    // A read-only mount check. Writing a probe file here wore the flash every
    // 30 s, and failed whenever the filesystem was merely full.
    return filesystem.is_mounted();
}

void FileIOTask::handle_filesystem_error() {
    // Runs on every health check while the filesystem is unavailable, so only
    // a recovery is logged; the mount itself reports each failure.
    if (attempt_filesystem_recovery()) {
        LOG_BLE("FileIOTask: Filesystem recovery successful\n");
    }
}

bool FileIOTask::attempt_filesystem_recovery() {
    // Only mount a filesystem that is not mounted. Never unmount one that the
    // web server, logger or screensaver may be using, and never format: that
    // would erase the grind history and the screensaver image.
    return filesystem.begin(false) && filesystem.is_mounted();
}

void FileIOTask::record_timing(uint32_t start_time, uint32_t end_time) {
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

void FileIOTask::print_heartbeat() const {
#if SYS_ENABLE_REALTIME_HEARTBEAT
    uint32_t avg_cycle_time = cycle_count > 0 ? cycle_time_sum_ms / cycle_count : 0;
    const char* fs_status = filesystem.is_mounted() ? "OK" : "ERROR";

    LOG_BLE("[%lums FILE_IO_HEARTBEAT] Cycles: %lu/10s | Avg: %lums (%lu-%lums) | FS: %s | Build: #%d\n",
           millis(), cycle_count, avg_cycle_time, cycle_time_min_ms, cycle_time_max_ms,
           fs_status, BUILD_NUMBER);
#endif
}

void FileIOTask::reset_performance_metrics() {
    cycle_count = 0;
    cycle_time_sum_ms = 0;
    cycle_time_min_ms = UINT32_MAX;
    cycle_time_max_ms = 0;
}
