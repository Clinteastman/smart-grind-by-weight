#pragma once
#include "../config/hardware.h"
#include <cstdint>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lvgl.h>
#include "touch_driver.h"
#include "../config/constants.h"

struct DisplayPerformanceSnapshot {
    uint32_t window_ms = 0;
    uint32_t ui_calls = 0;
    uint32_t refreshes = 0;
    uint32_t rendered_frames = 0;
    uint32_t flushes = 0;
    uint32_t pixels = 0;
    uint32_t ui_time_us = 0;
    uint32_t render_time_us = 0;
    uint32_t flush_wait_us = 0;  // time LVGL waited for the panel to take a buffer
};

/**
 * Owns the AMOLED panel, the LVGL display/input devices and the touch driver.
 *
 * Both board revisions drive a QSPI AMOLED controller through esp_lcd: V1 has a
 * CO5300 and V2 an SH8601. The two differ only in chip-select pin and power-on
 * command sequence, so everything past initialisation is shared.
 */
class DisplayManager {
private:
    esp_lcd_panel_io_handle_t panel_io;
    esp_lcd_panel_handle_t panel_handle;

    // Colour transfers complete asynchronously. LVGL flushes are acknowledged
    // from the completion callback; the startup splash waits on a semaphore
    // because it runs before the LVGL timer handler exists.
    lv_display_t* pending_flush_display;
    SemaphoreHandle_t direct_draw_done;
    volatile bool direct_draw_pending;

    lv_display_t* lvgl_display;
    lv_indev_t* lvgl_input;
    lv_color_t* draw_buffer;
    lv_color_t* second_draw_buffer;
    TouchDriver touch_driver;

    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t buffer_size;
    bool initialized;
    bool panel_powered_on;
    bool consume_wake_touch_until_release;
    uint32_t wake_touch_guard_started_ms;

    portMUX_TYPE metrics_mux = portMUX_INITIALIZER_UNLOCKED;
    DisplayPerformanceSnapshot metrics_window;
    DisplayPerformanceSnapshot metrics_snapshot;
    uint32_t metrics_window_started_ms = 0;
    uint32_t render_started_us = 0;
    uint32_t flush_wait_started_us = 0;

public:
    void init();
    void update();
    void set_brightness(float brightness);
    void set_panel_power(bool powered_on);
    bool is_panel_powered_on() const { return panel_powered_on; }
    bool draw_rgb565_file(const char* path, uint16_t width, uint16_t height);

    uint32_t get_width() const { return screen_width; }
    uint32_t get_height() const { return screen_height; }
    bool is_initialized() const { return initialized; }
    TouchDriver* get_touch_driver() { return &touch_driver; }
    DisplayPerformanceSnapshot get_performance_snapshot();

private:
    bool init_panel();
    /** Allocate the LVGL draw buffers, falling back to one if RAM is short. */
    bool allocate_draw_buffers();
    /** Push one RGB565 block and block until the panel has consumed it. */
    bool draw_bitmap_blocking(int x, int y, int width, int height, const void* pixels);

    static void display_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static bool color_transfer_done_cb(esp_lcd_panel_io_handle_t panel_io,
                                       esp_lcd_panel_io_event_data_t* event_data,
                                       void* user_context);
    static void display_rounder_cb(lv_event_t* e);
    static void display_metrics_cb(lv_event_t* e);
    static void touchpad_read_cb(lv_indev_t* indev, lv_indev_data_t* data);
    bool filter_touch_for_panel_wake(const TouchData& touch);
};

extern DisplayManager* g_display_manager;
