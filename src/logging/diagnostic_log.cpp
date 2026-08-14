#include "diagnostic_log.h"

#include <cstdarg>
#include <cstring>

namespace {

constexpr size_t DIAGNOSTIC_LOG_CAPACITY = 4096;
char recent_log[DIAGNOSTIC_LOG_CAPACITY];
size_t write_position = 0;
size_t retained_size = 0;
portMUX_TYPE log_mutex = portMUX_INITIALIZER_UNLOCKED;

void retain_message(const char* message, size_t length) {
    if (!message || length == 0) return;
    portENTER_CRITICAL(&log_mutex);
    for (size_t i = 0; i < length; ++i) {
        recent_log[write_position] = message[i];
        write_position = (write_position + 1) % DIAGNOSTIC_LOG_CAPACITY;
        if (retained_size < DIAGNOSTIC_LOG_CAPACITY) ++retained_size;
    }
    portEXIT_CRITICAL(&log_mutex);
}
}  // namespace

void diagnostic_log_write(const char* message) {
    if (!message) return;
    Serial.print(message);
    retain_message(message, strlen(message));
}

void diagnostic_log_printf(const char* format, ...) {
    if (!format) return;
    char message[512];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (written <= 0) return;
    const size_t length = static_cast<size_t>(written) < sizeof(message)
                              ? static_cast<size_t>(written)
                              : sizeof(message) - 1;
    Serial.write(reinterpret_cast<const uint8_t*>(message), length);
    retain_message(message, length);
}

String diagnostic_log_snapshot() {
    char snapshot[DIAGNOSTIC_LOG_CAPACITY + 1];
    size_t size = 0;
    size_t start = 0;
    portENTER_CRITICAL(&log_mutex);
    size = retained_size;
    start = (write_position + DIAGNOSTIC_LOG_CAPACITY - retained_size) %
            DIAGNOSTIC_LOG_CAPACITY;
    for (size_t i = 0; i < size; ++i) {
        snapshot[i] = recent_log[(start + i) % DIAGNOSTIC_LOG_CAPACITY];
    }
    portEXIT_CRITICAL(&log_mutex);
    snapshot[size] = '\0';
    return String(snapshot);
}
