#pragma once
#include <cstdint>
#include <cstring>
#include <limits>

// Only canonical session filenames may participate in export or retention.
// LittleFS versions return either a basename or the complete path.
inline bool parse_session_filename(const char* path, uint32_t& id) {
    if (!path) return false;
    const char* name = std::strrchr(path, '/');
    name = name ? name + 1 : path;
    if (std::strncmp(name, "session_", 8) != 0) return false;
    const char* digits = name + 8;
    if (*digits < '1' || *digits > '9') return false;
    uint32_t value = 0;
    while (*digits >= '0' && *digits <= '9') {
        const uint32_t digit = static_cast<uint32_t>(*digits++ - '0');
        if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    if (std::strcmp(digits, ".bin") != 0) return false;
    id = value;
    return true;
}
