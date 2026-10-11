#include "preferences.h"
#include <cstdint>

#include <esp_err.h>

#include <cstring>
#include <vector>

Preferences::~Preferences() {
    end();
}

bool Preferences::begin(const char* name, bool read_only) {
    if (open_ || !name) return false;

    const esp_err_t err =
        nvs_open(name, read_only ? NVS_READONLY : NVS_READWRITE, &handle_);
    if (err != ESP_OK) return false;

    open_ = true;
    read_only_ = read_only;
    return true;
}

void Preferences::end() {
    if (!open_) return;
    nvs_close(handle_);
    handle_ = 0;
    open_ = false;
}

bool Preferences::commit() {
    return nvs_commit(handle_) == ESP_OK;
}

bool Preferences::clear() {
    if (!open_ || read_only_) return false;
    return nvs_erase_all(handle_) == ESP_OK && commit();
}

bool Preferences::remove(const char* key) {
    if (!open_ || read_only_ || !key) return false;
    const esp_err_t err = nvs_erase_key(handle_, key);
    // Erasing a key that was never written is not a failure for callers that
    // just want it gone.
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    return err == ESP_OK && commit();
}

bool Preferences::isKey(const char* key) {
    if (!open_ || !key) return false;
    size_t length = 0;
    // A type-agnostic probe: ask for the entry as a blob and as a string, then
    // fall back to the fixed-width getters. nvs_find_key reports the stored
    // type directly and covers every case in one call.
    nvs_type_t type = NVS_TYPE_ANY;
    if (nvs_find_key(handle_, key, &type) == ESP_OK) return true;
    return nvs_get_blob(handle_, key, nullptr, &length) == ESP_OK;
}

bool Preferences::putUChar(const char* key, uint8_t value) {
    if (!open_ || read_only_ || !key) return false;
    return nvs_set_u8(handle_, key, value) == ESP_OK && commit();
}

bool Preferences::putUShort(const char* key, uint16_t value) {
    if (!open_ || read_only_ || !key) return false;
    return nvs_set_u16(handle_, key, value) == ESP_OK && commit();
}

bool Preferences::putInt(const char* key, int32_t value) {
    if (!open_ || read_only_ || !key) return false;
    return nvs_set_i32(handle_, key, value) == ESP_OK && commit();
}

bool Preferences::putUInt(const char* key, uint32_t value) {
    if (!open_ || read_only_ || !key) return false;
    return nvs_set_u32(handle_, key, value) == ESP_OK && commit();
}

bool Preferences::putULong64(const char* key, uint64_t value) {
    if (!open_ || read_only_ || !key) return false;
    return nvs_set_u64(handle_, key, value) == ESP_OK && commit();
}

bool Preferences::putBool(const char* key, bool value) {
    return putUChar(key, value ? 1 : 0);
}

bool Preferences::putFloat(const char* key, float value) {
    return putBytes(key, &value, sizeof(value));
}

bool Preferences::putString(const char* key, const char* value) {
    if (!open_ || read_only_ || !key || !value) return false;
    // An empty string is a legitimate value; NVS stores it as a lone terminator.
    return nvs_set_str(handle_, key, value) == ESP_OK && commit();
}

bool Preferences::putString(const char* key, const std::string& value) {
    return putString(key, value.c_str());
}

bool Preferences::putBytes(const char* key, const void* value, size_t length) {
    if (!open_ || read_only_ || !key || !value || length == 0) return false;
    return nvs_set_blob(handle_, key, value, length) == ESP_OK && commit();
}

int8_t Preferences::getChar(const char* key, int8_t default_value) {
    if (!open_ || !key) return default_value;
    int8_t value = default_value;
    return nvs_get_i8(handle_, key, &value) == ESP_OK ? value : default_value;
}

uint8_t Preferences::getUChar(const char* key, uint8_t default_value) {
    if (!open_ || !key) return default_value;
    uint8_t value = default_value;
    return nvs_get_u8(handle_, key, &value) == ESP_OK ? value : default_value;
}

int16_t Preferences::getShort(const char* key, int16_t default_value) {
    if (!open_ || !key) return default_value;
    int16_t value = default_value;
    return nvs_get_i16(handle_, key, &value) == ESP_OK ? value : default_value;
}

uint16_t Preferences::getUShort(const char* key, uint16_t default_value) {
    if (!open_ || !key) return default_value;
    uint16_t value = default_value;
    return nvs_get_u16(handle_, key, &value) == ESP_OK ? value : default_value;
}

int32_t Preferences::getInt(const char* key, int32_t default_value) {
    if (!open_ || !key) return default_value;
    int32_t value = default_value;
    return nvs_get_i32(handle_, key, &value) == ESP_OK ? value : default_value;
}

uint32_t Preferences::getUInt(const char* key, uint32_t default_value) {
    if (!open_ || !key) return default_value;
    uint32_t value = default_value;
    return nvs_get_u32(handle_, key, &value) == ESP_OK ? value : default_value;
}

int64_t Preferences::getLong64(const char* key, int64_t default_value) {
    if (!open_ || !key) return default_value;
    int64_t value = default_value;
    return nvs_get_i64(handle_, key, &value) == ESP_OK ? value : default_value;
}

uint64_t Preferences::getULong64(const char* key, uint64_t default_value) {
    if (!open_ || !key) return default_value;
    uint64_t value = default_value;
    return nvs_get_u64(handle_, key, &value) == ESP_OK ? value : default_value;
}

bool Preferences::getBool(const char* key, bool default_value) {
    return getUChar(key, default_value ? 1 : 0) == 1;
}

float Preferences::getFloat(const char* key, float default_value) {
    float value = default_value;
    getBytes(key, &value, sizeof(value));
    return value;
}

double Preferences::getDouble(const char* key, double default_value) {
    double value = default_value;
    getBytes(key, &value, sizeof(value));
    return value;
}

std::string Preferences::getString(const char* key, const char* default_value) {
    if (!open_ || !key) return default_value ? default_value : "";

    size_t length = 0;
    if (nvs_get_str(handle_, key, nullptr, &length) != ESP_OK || length == 0) {
        return default_value ? default_value : "";
    }

    std::vector<char> buffer(length);
    if (nvs_get_str(handle_, key, buffer.data(), &length) != ESP_OK) {
        return default_value ? default_value : "";
    }
    // NVS reports the length including the terminator.
    return std::string(buffer.data());
}

std::string Preferences::getString(const char* key, const std::string& default_value) {
    return getString(key, default_value.c_str());
}

size_t Preferences::getBytes(const char* key, void* buffer, size_t max_length) {
    if (!open_ || !key) return 0;
    if (!buffer || max_length == 0) return getBytesLength(key);

    // One lookup: NVS reports ESP_ERR_NVS_INVALID_LENGTH and the required size
    // when the buffer is too small, which is the partial-read guard we want.
    // Every float and double is stored as a blob, so this is a hot path.
    size_t length = max_length;
    const esp_err_t err = nvs_get_blob(handle_, key, buffer, &length);
    if (err != ESP_OK) return 0;
    return length;
}

size_t Preferences::getBytesLength(const char* key) {
    if (!open_ || !key) return 0;
    size_t length = 0;
    if (nvs_get_blob(handle_, key, nullptr, &length) != ESP_OK) return 0;
    return length;
}
