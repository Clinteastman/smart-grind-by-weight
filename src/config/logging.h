#pragma once

// Forward declaration to avoid circular dependency
class BluetoothManager;
extern BluetoothManager g_bluetooth_manager;

#include <Arduino.h>
#ifndef SMART_GRIND_SIM
#include "../logging/diagnostic_log.h"

#define LOG_BLE(format, ...) diagnostic_log_printf(format, ##__VA_ARGS__)
#else
#define LOG_BLE(format, ...) Serial.printf(format, ##__VA_ARGS__)
#endif

// Replace DEBUG macros to use Serial logging
#if DEBUG_SERIAL_OUTPUT
#define LOG_DEBUG_PRINTF(format, ...) Serial.printf(format, ##__VA_ARGS__)
#define LOG_DEBUG_PRINTLN(str) Serial.println(str)
#define LOG_DEBUG_PRINT(str) Serial.print(str)
#else
#define LOG_DEBUG_PRINTF(format, ...)
#define LOG_DEBUG_PRINTLN(str)
#define LOG_DEBUG_PRINT(str)
#endif

// Conditional debug macros for different subsystems
#if DEBUG_GRIND_CONTROLLER
#define LOG_GRIND_DEBUG(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
#define LOG_GRIND_DEBUG(format, ...)
#endif

#if DEBUG_LOAD_CELL
#define LOG_LOADCELL_DEBUG(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
#define LOG_LOADCELL_DEBUG(format, ...)
#endif

#if DEBUG_UI_SYSTEM
#define LOG_UI_DEBUG(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
#define LOG_UI_DEBUG(format, ...)
#endif

#if DEBUG_CALIBRATION
#define LOG_CALIBRATION_DEBUG(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
#define LOG_CALIBRATION_DEBUG(format, ...)
#endif

#if DEBUG_WEIGHT_SETTLING
#define LOG_SETTLING_DEBUG(format, ...) Serial.printf(format, ##__VA_ARGS__)
#else
#define LOG_SETTLING_DEBUG(format, ...)
#endif

#ifdef ENABLE_BLE_DEBUG_VERBOSE
#ifndef SMART_GRIND_SIM
#define LOG_BLE_DEBUG(format, ...) diagnostic_log_printf(format, ##__VA_ARGS__)
#else
#define LOG_BLE_DEBUG(format, ...) Serial.printf(format, ##__VA_ARGS__)
#endif
#else
#define LOG_BLE_DEBUG(format, ...)
#endif

#ifndef SMART_GRIND_SIM
#define LOG_OTA_DEBUG(format, ...) diagnostic_log_printf("[OTA_DEBUG] " format, ##__VA_ARGS__)
#else
#define LOG_OTA_DEBUG(format, ...) Serial.printf("[OTA_DEBUG] " format, ##__VA_ARGS__)
#endif
