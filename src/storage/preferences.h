#pragma once

#include <nvs.h>
#include <stddef.h>
#include <stdint.h>
#include <string>

//==============================================================================
// PERSISTENT SETTINGS STORE
//==============================================================================
// A namespaced key/value store on top of ESP-IDF's NVS, used for calibration,
// grind profiles, Wi-Fi credentials, display settings and lifetime statistics.
//
// The on-flash encoding is deliberately identical to the layout this firmware
// wrote before the ESP-IDF migration: bool and uchar as u8, short as u16, int as
// i32/u32, float and double as blobs, strings as NVS strings. Changing an
// encoding here would make an existing grinder lose its calibration and history
// the first time it boots new firmware.
//
// NVS limits both namespace and key names to 15 characters. Writes commit
// immediately so a power loss mid-setting cannot leave a torn value.

class Preferences {
public:
    Preferences() = default;
    ~Preferences();

    Preferences(const Preferences&) = delete;
    Preferences& operator=(const Preferences&) = delete;

    /**
     * Open a namespace. Opening read-only fails when the namespace has never
     * been written, which callers use to detect first-boot defaults.
     */
    bool begin(const char* name, bool read_only = false);
    void end();

    bool clear();
    bool remove(const char* key);
    bool isKey(const char* key);

    // Writes report success, not a byte count. A byte count cannot distinguish
    // failure from successfully storing an empty string, which is exactly what
    // an open Wi-Fi network's password is.
    //
    // The getters cover more NVS types than the setters because the BLE
    // diagnostics dump reads back keys of any type this firmware never writes.
    bool putUChar(const char* key, uint8_t value);
    bool putUShort(const char* key, uint16_t value);
    bool putInt(const char* key, int32_t value);
    bool putUInt(const char* key, uint32_t value);
    bool putULong64(const char* key, uint64_t value);
    bool putBool(const char* key, bool value);
    bool putFloat(const char* key, float value);
    bool putString(const char* key, const char* value);
    bool putString(const char* key, const std::string& value);
    bool putBytes(const char* key, const void* value, size_t length);

    // Reads fall back to the supplied default when the key is absent or has an
    // unexpected type.
    int8_t getChar(const char* key, int8_t default_value = 0);
    uint8_t getUChar(const char* key, uint8_t default_value = 0);
    int16_t getShort(const char* key, int16_t default_value = 0);
    uint16_t getUShort(const char* key, uint16_t default_value = 0);
    int32_t getInt(const char* key, int32_t default_value = 0);
    uint32_t getUInt(const char* key, uint32_t default_value = 0);
    int64_t getLong64(const char* key, int64_t default_value = 0);
    uint64_t getULong64(const char* key, uint64_t default_value = 0);
    bool getBool(const char* key, bool default_value = false);
    float getFloat(const char* key, float default_value = 0.0f);
    double getDouble(const char* key, double default_value = 0.0);
    std::string getString(const char* key, const char* default_value = "");
    std::string getString(const char* key, const std::string& default_value);
    size_t getBytes(const char* key, void* buffer, size_t max_length);
    size_t getBytesLength(const char* key);

private:
    /** Commit the open handle, returning false when the write did not stick. */
    bool commit();

    nvs_handle_t handle_ = 0;
    bool open_ = false;
    bool read_only_ = false;
};
