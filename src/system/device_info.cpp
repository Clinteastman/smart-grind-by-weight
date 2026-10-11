#include "device_info.h"
#include <cstdint>

#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <soc/rtc.h>

namespace device_info {

size_t free_heap_bytes() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

size_t total_heap_bytes() {
    return heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
}

size_t free_internal_heap_bytes() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

size_t largest_free_internal_block_bytes() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

size_t free_psram_bytes() {
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

size_t flash_chip_size_bytes() {
    uint32_t size = 0;
    if (esp_flash_get_size(nullptr, &size) != ESP_OK) return 0;
    return size;
}

uint64_t efuse_mac() {
    uint8_t mac[6] = {};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return 0;
    // Last byte first, so mac[0] lands in the least significant byte.
    uint64_t value = 0;
    for (int i = 5; i >= 0; --i) {
        value = (value << 8) | mac[i];
    }
    return value;
}

uint32_t cpu_frequency_mhz() {
    rtc_cpu_freq_config_t config = {};
    rtc_clk_cpu_freq_get_config(&config);
    return config.freq_mhz;
}

}  // namespace device_info
