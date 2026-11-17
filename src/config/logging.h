#pragma once

#include <stdio.h>

// Diagnostics are written to the console with plain printf. On device the
// ESP-IDF console is the native USB-Serial-JTAG port; on the desktop simulator
// it is stdout.
#ifndef SMART_GRIND_SIM
#include "../logging/diagnostic_log.h"

#define LOG_BLE(format, ...) diagnostic_log_printf(format, ##__VA_ARGS__)
#else
#define LOG_BLE(format, ...) printf(format, ##__VA_ARGS__)
#endif

// Console-only logging, compiled out unless the matching debug switch is on.
#if DEBUG_SERIAL_OUTPUT
#define LOG_DEBUG_PRINTF(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOG_DEBUG_PRINTF(format, ...)
#endif

// Conditional debug macros for different subsystems
#if DEBUG_GRIND_CONTROLLER
#define LOG_GRIND_DEBUG(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOG_GRIND_DEBUG(format, ...)
#endif

#if DEBUG_LOAD_CELL
#define LOG_LOADCELL_DEBUG(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOG_LOADCELL_DEBUG(format, ...)
#endif

#if DEBUG_UI_SYSTEM
#define LOG_UI_DEBUG(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOG_UI_DEBUG(format, ...)
#endif

#if DEBUG_CALIBRATION
#define LOG_CALIBRATION_DEBUG(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOG_CALIBRATION_DEBUG(format, ...)
#endif

#if DEBUG_WEIGHT_SETTLING
#define LOG_SETTLING_DEBUG(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOG_SETTLING_DEBUG(format, ...)
#endif

#ifdef ENABLE_BLE_DEBUG_VERBOSE
#ifndef SMART_GRIND_SIM
#define LOG_BLE_DEBUG(format, ...) diagnostic_log_printf(format, ##__VA_ARGS__)
#else
#define LOG_BLE_DEBUG(format, ...) printf(format, ##__VA_ARGS__)
#endif
#else
#define LOG_BLE_DEBUG(format, ...)
#endif

#ifndef SMART_GRIND_SIM
#define LOG_OTA_DEBUG(format, ...) diagnostic_log_printf("[OTA_DEBUG] " format, ##__VA_ARGS__)
#else
#define LOG_OTA_DEBUG(format, ...) printf("[OTA_DEBUG] " format, ##__VA_ARGS__)
#endif
