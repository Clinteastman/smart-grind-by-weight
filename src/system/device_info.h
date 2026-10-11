#pragma once

#include <stddef.h>
#include <stdint.h>

//==============================================================================
// SYSTEM AND CHIP INFORMATION
//==============================================================================
// Thin, self-documenting accessors over the ESP-IDF heap, efuse, flash and clock
// APIs. The diagnostics screen, BLE system-info service and the HTTP status
// endpoint all report these values.

namespace device_info {

/**
 * Free bytes of internal RAM, excluding PSRAM. This is the figure the
 * heartbeat, BLE system info, status API and menu have always reported
 * (Arduino's ESP.getFreeHeap()), so readings compare across firmware versions.
 */
size_t free_heap_bytes();

/** Total bytes of internal RAM, excluding PSRAM (Arduino's ESP.getHeapSize()). */
size_t total_heap_bytes();

/**
 * Free bytes of byte-addressable internal RAM, the measure the OTA admission
 * checks and the status API's free_internal_heap field use.
 */
size_t free_internal_heap_bytes();

/** Largest single allocatable block of internal RAM. */
size_t largest_free_internal_block_bytes();

/** Free bytes of external PSRAM, or 0 when PSRAM is unavailable. */
size_t free_psram_bytes();

/** Size of the attached SPI flash chip in bytes. */
size_t flash_chip_size_bytes();

/**
 * Factory-programmed 48-bit MAC address, used as the stable device identifier.
 * The first MAC byte is the least significant, as Arduino's ESP.getEfuseMac()
 * returned it: the setup hotspot name and the device_id reported over the API
 * and mDNS derive from this value, so a different order renames a grinder.
 */
uint64_t efuse_mac();

/** Current CPU frequency in MHz. */
uint32_t cpu_frequency_mhz();

}  // namespace device_info
