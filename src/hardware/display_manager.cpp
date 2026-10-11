#include "display_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>

#include <driver/spi_master.h>
#include <esp_heap_caps.h>

#include <algorithm>

#include "touch_wake_policy.h"
#include "../config/constants.h"
#include "../storage/filesystem.h"
#include "../system/timing.h"

#if HW_DISPLAY_VARIANT_V2
#include <esp_lcd_sh8601.h>
#else
#include <esp_lcd_co5300.h>
#endif

DisplayManager* g_display_manager = nullptr;

namespace {

constexpr spi_host_device_t kDisplaySpiHost = SPI2_HOST;

// The two controllers expose the same QSPI framing and register set, so only
// the vendor types, factory function and power-on sequence differ.
#if HW_DISPLAY_VARIANT_V2

using PanelInitCmd = sh8601_lcd_init_cmd_t;
using PanelVendorConfig = sh8601_vendor_config_t;
constexpr auto kNewPanel = esp_lcd_new_panel_sh8601;

static const uint8_t kCmdC4[] = {0x80};
static const uint8_t kCmd35[] = {0x00};
static const uint8_t kCmd53[] = {0x20};
static const uint8_t kCmd63[] = {0xFF};
static const uint8_t kBrightnessOff[] = {0x00};
static const uint8_t kBrightnessFull[] = {0xFF};

// Waveshare V2 (SH8601) power-on sequence.
static const PanelInitCmd kInitCommands[] = {
    {0x11, nullptr, 0, 80},                            // Sleep out
    {0xC4, kCmdC4, sizeof(kCmdC4), 0},                 // SPI/QSPI mode control
    {0x35, kCmd35, sizeof(kCmd35), 0},                 // Tearing effect on
    {0x53, kCmd53, sizeof(kCmd53), 1},                 // Brightness control on
    {0x63, kCmd63, sizeof(kCmd63), 1},                 // High-brightness mode
    {0x51, kBrightnessOff, sizeof(kBrightnessOff), 1},  // Dark before display on
    {0x29, nullptr, 0, 10},                            // Display on
    {0x51, kBrightnessFull, sizeof(kBrightnessFull), 0},
};

#else

using PanelInitCmd = co5300_lcd_init_cmd_t;
using PanelVendorConfig = co5300_vendor_config_t;
constexpr auto kNewPanel = esp_lcd_new_panel_co5300;

static const uint8_t kCmdFE[] = {0x00};
static const uint8_t kCmdC4[] = {0x80};
static const uint8_t kCmd53[] = {0x20};
static const uint8_t kCmd63[] = {0xFF};
static const uint8_t kBrightnessDefault[] = {0xD0};
static const uint8_t kContrastOff[] = {0x00};

// Waveshare V1 (CO5300) power-on sequence. The stock driver default targets a
// 466x466 module, so this reproduces the sequence the 280x456 panel needs.
// esp_lcd sends MADCTL and COLMOD (16 bpp) ahead of these commands.
static const PanelInitCmd kInitCommands[] = {
    {0x11, nullptr, 0, 120},                                  // Sleep out
    {0xFE, kCmdFE, sizeof(kCmdFE), 0},                        // Select page 0
    {0xC4, kCmdC4, sizeof(kCmdC4), 0},                        // SPI/QSPI mode control
    {0x53, kCmd53, sizeof(kCmd53), 0},                        // Brightness control on
    {0x63, kCmd63, sizeof(kCmd63), 0},                        // High-brightness mode
    {0x29, nullptr, 0, 10},                                   // Display on
    {0x51, kBrightnessDefault, sizeof(kBrightnessDefault), 0},
    {0x58, kContrastOff, sizeof(kContrastOff), 0},            // Sunlight enhancement off
    {0x20, nullptr, 0, 10},                                   // Inversion off
};

#endif

/**
 * QSPI command word for a parameter write: instruction 0x02, 16-bit register
 * address, one dummy byte.
 */
constexpr uint32_t qspi_param_command(uint8_t reg) {
    return (0x02UL << 24) | (static_cast<uint32_t>(reg) << 8);
}

constexpr uint8_t kCmdBrightness = 0x51;

}  // namespace

bool DisplayManager::init_panel() {
    spi_bus_config_t bus_config = {};
    bus_config.sclk_io_num = HW_DISPLAY_SCK_PIN;
    bus_config.data0_io_num = HW_DISPLAY_D0_PIN;
    bus_config.data1_io_num = HW_DISPLAY_D1_PIN;
    bus_config.data2_io_num = HW_DISPLAY_D2_PIN;
    bus_config.data3_io_num = HW_DISPLAY_D3_PIN;
    bus_config.max_transfer_sz =
        HW_DISPLAY_WIDTH_PX * HW_DISPLAY_DRAW_BUFFER_ROWS * sizeof(uint16_t);
    if (spi_bus_initialize(kDisplaySpiHost, &bus_config, SPI_DMA_CH_AUTO) != ESP_OK) {
        LOG_BLE("[DISPLAY] ERROR: QSPI bus initialisation failed\n");
        return false;
    }

    // esp_lcd takes pins as gpio_num_t; the QSPI panel has no D/C line.
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = static_cast<gpio_num_t>(HW_DISPLAY_CS_PIN);
    io_config.dc_gpio_num = GPIO_NUM_NC;
    io_config.spi_mode = 0;
    io_config.pclk_hz = HW_DISPLAY_QSPI_FREQUENCY_HZ;
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = color_transfer_done_cb;
    io_config.user_ctx = this;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;
    if (esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(kDisplaySpiHost),
                                 &io_config, &panel_io) != ESP_OK) {
        LOG_BLE("[DISPLAY] ERROR: panel IO creation failed\n");
        return false;
    }

    PanelVendorConfig vendor_config = {};
    vendor_config.init_cmds = kInitCommands;
    vendor_config.init_cmds_size = sizeof(kInitCommands) / sizeof(kInitCommands[0]);
    vendor_config.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = static_cast<gpio_num_t>(HW_DISPLAY_RESET_PIN);
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config;

    if (kNewPanel(panel_io, &panel_config, &panel_handle) != ESP_OK ||
        esp_lcd_panel_reset(panel_handle) != ESP_OK ||
        esp_lcd_panel_init(panel_handle) != ESP_OK) {
        LOG_BLE("[DISPLAY] ERROR: panel initialisation failed\n");
        return false;
    }

    // The visible 280 px window starts partway into panel RAM on both revisions.
    esp_lcd_panel_set_gap(panel_handle, HW_DISPLAY_OFFSET_X_PX, 0);
    return esp_lcd_panel_disp_on_off(panel_handle, true) == ESP_OK;
}

void DisplayManager::init() {
    g_display_manager = this;
    panel_io = nullptr;
    panel_handle = nullptr;
    pending_flush_display = nullptr;
    direct_draw_pending = false;
    draw_buffer = nullptr;
    second_draw_buffer = nullptr;
    initialized = false;
    panel_powered_on = true;
    consume_wake_touch_until_release = false;
    wake_touch_guard_started_ms = 0;

    direct_draw_done = xSemaphoreCreateBinary();
    if (!direct_draw_done) {
        LOG_BLE("[DISPLAY] ERROR: Failed to create transfer semaphore\n");
        return;
    }

    if (!init_panel()) return;

    lv_init();
    lv_tick_set_cb(millis);

    screen_width = HW_DISPLAY_WIDTH_PX;
    screen_height = HW_DISPLAY_HEIGHT_PX;

    if (!allocate_draw_buffers()) {
        LOG_BLE("[DISPLAY] ERROR: Failed to allocate LVGL draw buffer\n");
        return;
    }

    lvgl_display = lv_display_create(screen_width, screen_height);
    lv_display_set_flush_cb(lvgl_display, display_flush_cb);
    lv_display_set_buffers(lvgl_display, draw_buffer, second_draw_buffer,
                          buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_add_event_cb(lvgl_display, display_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_add_event_cb(lvgl_display, display_metrics_cb, LV_EVENT_ALL, NULL);

    // Initialize touch
    touch_driver.init();
    lvgl_input = lv_indev_create();
    lv_indev_set_type(lvgl_input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_input, touchpad_read_cb);

    // The UI task runs LVGL every SYS_TASK_UI_INTERVAL_MS. LVGL's refresh,
    // input and animation timers default to the same period, so a little
    // scheduling jitter made them miss a UI cycle and a ready frame waited
    // twice as long. With shorter periods every UI cycle can draw a frame.
    lv_timer_set_period(lv_display_get_refr_timer(lvgl_display), SYS_LVGL_TIMER_PERIOD_MS);
    lv_timer_set_period(lv_indev_get_read_timer(lvgl_input), SYS_LVGL_TIMER_PERIOD_MS);
    lv_timer_set_period(lv_anim_get_timer(), SYS_LVGL_TIMER_PERIOD_MS);

    initialized = true;
}

bool DisplayManager::allocate_draw_buffers() {
    static_assert(HW_DISPLAY_DRAW_BUFFER_ROWS % 2 == 0,
                  "strips must keep the panel's 2-row window alignment");
    // LVGL renders in partial mode straight into DMA-capable internal RAM, so
    // a flush is a single queued transfer with no intermediate copy. With two
    // buffers it draws the next strip while the previous one is being sent.
    buffer_size = screen_width * HW_DISPLAY_DRAW_BUFFER_ROWS * sizeof(uint16_t);
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
    draw_buffer = static_cast<lv_color_t*>(heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, buffer_size, caps));
    if (!draw_buffer) return false;
    second_draw_buffer =
        static_cast<lv_color_t*>(heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, buffer_size, caps));
    if (!second_draw_buffer) {
        LOG_BLE("[DISPLAY] Not enough internal RAM for a second draw buffer\n");
    }
    return true;
}

void DisplayManager::update() {
    if (!initialized) return;

    const uint32_t started_us = micros();
    touch_driver.update();
    lv_timer_handler();

    const uint32_t now_ms = millis();
    metrics_window.ui_calls++;
    metrics_window.ui_time_us += micros() - started_us;
    if (metrics_window_started_ms == 0) {
        metrics_window_started_ms = now_ms;
    } else if (now_ms - metrics_window_started_ms >= 1000) {
        metrics_window.window_ms = now_ms - metrics_window_started_ms;
        portENTER_CRITICAL(&metrics_mux);
        metrics_snapshot = metrics_window;
        portEXIT_CRITICAL(&metrics_mux);
        metrics_window = {};
        metrics_window_started_ms = now_ms;
    }
}

DisplayPerformanceSnapshot DisplayManager::get_performance_snapshot() {
    portENTER_CRITICAL(&metrics_mux);
    const DisplayPerformanceSnapshot snapshot = metrics_snapshot;
    portEXIT_CRITICAL(&metrics_mux);
    return snapshot;
}

bool DisplayManager::draw_bitmap_blocking(int x, int y, int width, int height,
                                          const void* pixels) {
    direct_draw_pending = true;
    if (esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + width, y + height, pixels) != ESP_OK) {
        direct_draw_pending = false;
        return false;
    }
    // A queued transfer reads straight out of `pixels`, so the caller may not
    // reuse the buffer until the panel signals completion.
    if (xSemaphoreTake(direct_draw_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        direct_draw_pending = false;
        return false;
    }
    return true;
}

bool DisplayManager::draw_rgb565_file(const char* path, uint16_t width, uint16_t height) {
    if (!panel_handle || !path || width == 0 || height == 0) return false;

    const size_t expected_size = static_cast<size_t>(width) * height * sizeof(uint16_t);
    FsFile file = filesystem.open(path, "r");
    if (!file) {
        LOG_BLE("[DISPLAY] RGB565 draw skipped: failed to open %s\n", path);
        return false;
    }

    if (file.size() != expected_size) {
        LOG_BLE("[DISPLAY] RGB565 draw skipped: %s size %u != %u\n",
                path,
                static_cast<unsigned>(file.size()),
                static_cast<unsigned>(expected_size));
        return false;
    }

    uint16_t rows_per_chunk = HW_DISPLAY_DRAW_BUFFER_ROWS;
    uint16_t* row_buffer = nullptr;
    while (rows_per_chunk >= 4 && row_buffer == nullptr) {
        row_buffer = static_cast<uint16_t*>(
            heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                    static_cast<size_t>(width) * rows_per_chunk * sizeof(uint16_t),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        if (!row_buffer) {
            rows_per_chunk /= 2;
        }
    }

    if (!row_buffer) {
        LOG_BLE("[DISPLAY] RGB565 draw skipped: row buffer allocation failed\n");
        return false;
    }

    const int x = (screen_width > width) ? static_cast<int>((screen_width - width) / 2) : 0;
    const int y = (screen_height > height) ? static_cast<int>((screen_height - height) / 2) : 0;
    bool success = true;

    for (uint16_t current_y = 0; current_y < height; current_y += rows_per_chunk) {
        const uint16_t rows = std::min<uint16_t>(rows_per_chunk, height - current_y);
        const size_t bytes_to_read = static_cast<size_t>(width) * rows * sizeof(uint16_t);
        if (file.read(reinterpret_cast<uint8_t*>(row_buffer), bytes_to_read) != bytes_to_read) {
            LOG_BLE("[DISPLAY] RGB565 draw aborted: short read at row %u\n", current_y);
            success = false;
            break;
        }

        // Screensaver uploads are stored in the panel's native RGB565 byte
        // order, so the rows go out without conversion.
        if (!draw_bitmap_blocking(x, y + current_y, width, rows, row_buffer)) {
            LOG_BLE("[DISPLAY] RGB565 draw aborted: transfer failed at row %u\n", current_y);
            success = false;
            break;
        }
    }

    heap_caps_free(row_buffer);
    return success;
}

// Redraw full-width rows, starting on an even row and ending on an odd one:
// partial-width updates leave artifacts, and the SH8601 (V2) only accepts
// drawing windows on 2-pixel boundaries.
void DisplayManager::display_rounder_cb(lv_event_t* e) {
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);

    area->x1 = 0;
    area->x2 = g_display_manager->screen_width - 1;
    area->y1 &= ~1;
    area->y2 = std::min<int32_t>(area->y2 | 1, g_display_manager->screen_height - 1);
}

void DisplayManager::display_metrics_cb(lv_event_t* e) {
    if (!g_display_manager) return;

    switch (lv_event_get_code(e)) {
        case LV_EVENT_REFR_START:
            g_display_manager->metrics_window.refreshes++;
            break;
        case LV_EVENT_RENDER_START:
            g_display_manager->metrics_window.rendered_frames++;
            g_display_manager->render_started_us = micros();
            break;
        case LV_EVENT_RENDER_READY:
            if (g_display_manager->render_started_us != 0) {
                g_display_manager->metrics_window.render_time_us +=
                    micros() - g_display_manager->render_started_us;
                g_display_manager->render_started_us = 0;
            }
            break;
        // Transfers run in the background, so the panel only costs the UI the
        // time LVGL waits for a transfer to free a draw buffer.
        case LV_EVENT_FLUSH_WAIT_START:
            g_display_manager->flush_wait_started_us = micros();
            break;
        case LV_EVENT_FLUSH_WAIT_FINISH:
            g_display_manager->metrics_window.flush_wait_us +=
                micros() - g_display_manager->flush_wait_started_us;
            break;
        default:
            break;
    }
}

void DisplayManager::display_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    if (!g_display_manager || !g_display_manager->panel_handle) {
        lv_display_flush_ready(disp);
        return;
    }

    const uint32_t width = lv_area_get_width(area);
    const uint32_t height = lv_area_get_height(area);
    g_display_manager->metrics_window.flushes++;
    g_display_manager->metrics_window.pixels += width * height;

    g_display_manager->pending_flush_display = disp;
    if (esp_lcd_panel_draw_bitmap(g_display_manager->panel_handle,
                                  area->x1, area->y1,
                                  area->x2 + 1, area->y2 + 1,
                                  px_map) != ESP_OK) {
        g_display_manager->pending_flush_display = nullptr;
        lv_display_flush_ready(disp);
    }
}

bool DisplayManager::color_transfer_done_cb(esp_lcd_panel_io_handle_t,
                                            esp_lcd_panel_io_event_data_t*,
                                            void* user_context) {
    DisplayManager* manager = static_cast<DisplayManager*>(user_context);
    if (!manager) return false;

    if (manager->direct_draw_pending) {
        manager->direct_draw_pending = false;
        BaseType_t higher_priority_task_woken = pdFALSE;
        xSemaphoreGiveFromISR(manager->direct_draw_done, &higher_priority_task_woken);
        return higher_priority_task_woken == pdTRUE;
    }

    if (manager->pending_flush_display) {
        lv_display_t* display = manager->pending_flush_display;
        manager->pending_flush_display = nullptr;
        lv_display_flush_ready(display);
    }
    return false;
}

void DisplayManager::touchpad_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    if (!g_display_manager) return;

    TouchData touch = g_display_manager->touch_driver.get_touch_data();

    if (g_display_manager->filter_touch_for_panel_wake(touch)) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (touch.pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = touch.x;
        data->point.y = touch.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void DisplayManager::set_brightness(float brightness) {
    if (!panel_io) return;

    // Clamp brightness to valid hardware range [0.0, 1.0]
    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;

    uint8_t brightness_value = (uint8_t)(brightness * 255.0f);
    esp_lcd_panel_io_tx_param(panel_io, qspi_param_command(kCmdBrightness),
                              &brightness_value, sizeof(brightness_value));
}

void DisplayManager::set_panel_power(bool powered_on) {
    if (!initialized || !panel_handle) return;
    if (panel_powered_on == powered_on) return;

    if (esp_lcd_panel_disp_on_off(panel_handle, powered_on) != ESP_OK) {
        LOG_BLE("[DISPLAY] Failed to turn panel %s\n", powered_on ? "on" : "off");
        return;
    }

    panel_powered_on = powered_on;
    if (powered_on && lvgl_display) {
        lv_obj_invalidate(lv_screen_active());
    }
    LOG_BLE("[DISPLAY] Panel %s\n", powered_on ? "on" : "off");
}

bool DisplayManager::filter_touch_for_panel_wake(const TouchData& touch) {
    if (!panel_powered_on) {
        if (touch.just_pressed) {
            set_panel_power(true);
            touch_driver.consume_press_event();
            consume_wake_touch_until_release = true;
            wake_touch_guard_started_ms = millis();
            LOG_BLE("[DISPLAY] Panel wake source: touch\n");
        }
        return true;
    }

    if (!consume_wake_touch_until_release) {
        if (touch.just_pressed) {
            touch_driver.consume_press_event();
        }
        return false;
    }
    if (!touch.pressed) {
        consume_wake_touch_until_release = false;
        wake_touch_guard_started_ms = 0;
        return true;
    }

    // Do not let a stale controller contact suppress every future gesture.
    // One second is comfortably longer than a normal wake tap, while keeping
    // recovery finite if the controller never reports the matching release.
    if (wake_touch_guard_expired(millis() - wake_touch_guard_started_ms)) {
        consume_wake_touch_until_release = false;
        wake_touch_guard_started_ms = 0;
        LOG_BLE("[DISPLAY] Wake-touch guard recovered after missing release\n");
    }
    return true;
}
