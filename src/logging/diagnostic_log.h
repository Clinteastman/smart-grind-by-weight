#pragma once

#include <Arduino.h>

// Writes to the existing USB serial console and retains a bounded recent-log
// snapshot for read-only Wi-Fi diagnostics. The buffer is intentionally kept
// in RAM so normal logging never adds flash wear.
#if defined(__GNUC__)
#define SMART_GRIND_PRINTF_FORMAT __attribute__((format(printf, 1, 2)))
#else
#define SMART_GRIND_PRINTF_FORMAT
#endif
void diagnostic_log_printf(const char* format, ...) SMART_GRIND_PRINTF_FORMAT;
void diagnostic_log_write(const char* message);
String diagnostic_log_snapshot();
#undef SMART_GRIND_PRINTF_FORMAT
