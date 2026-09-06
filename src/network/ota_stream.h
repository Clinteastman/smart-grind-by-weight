#pragma once

#include <cstdint>

// TLS readBytes can return immediately when the body has not arrived yet.
// Wait for readable data, including bytes buffered after the peer disconnects.
template <typename Stream, typename Clock, typename Pause>
bool ota_wait_for_data(Stream& stream, Clock now, Pause pause, uint32_t timeout_ms) {
    const uint32_t started = now();
    while (true) {
        if (stream.available() > 0) return true;
        if (!stream.connected() || static_cast<uint32_t>(now() - started) >= timeout_ms) {
            return false;
        }
        pause();
    }
}
