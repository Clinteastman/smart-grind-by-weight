#pragma once

#include <lvgl.h>
#include <Arduino.h>
#include <cstdint>

/**
 * ScreensaverController - Loads and displays a custom screensaver image
 * from LittleFS as a full-screen LVGL overlay.
 *
 * Used for startup splash and power-save display modes.
 */
class ScreensaverController {
public:
    ScreensaverController();
    ~ScreensaverController();

    /// Check if a screensaver image file exists on LittleFS
    bool has_image() const;

    /// Read NVS preference: show image on startup
    bool is_startup_enabled() const;

    /// Read NVS preference: show image during sleep/dim
    bool is_sleep_enabled() const;

    /// Read NVS preference: startup display duration in milliseconds
    uint32_t get_startup_timeout_ms() const;

    /// Load image from LittleFS and display as full-screen overlay
    void show();

    /// Hide overlay and free image buffer
    void hide();

    bool is_visible() const { return visible_; }

private:
    lv_obj_t* overlay_screen_;
    lv_obj_t* previous_screen_;
    lv_obj_t* image_widget_;
    lv_obj_t* primary_label_;
    lv_obj_t* secondary_label_;
    lv_obj_t* detail_label_;
    lv_obj_t* hours_label_;
    lv_obj_t* colon_label_;
    lv_obj_t* minutes_label_;
    lv_timer_t* animation_timer_;
    uint8_t* image_buffer_;
    lv_image_dsc_t image_dsc_;
    String style_;
    uint8_t animation_step_;
    bool visible_;

    String selected_style() const;
    String selected_clock_position() const;
    String selected_clock_layout() const;
    uint32_t selected_clock_color() const;
    String selected_clock_font() const;
    uint32_t clock_color_ = 0xFFFFFF;
    String clock_font_choice_ = "montserrat";
    uint8_t* clock_font_buffer_ = nullptr;
    size_t clock_font_size_ = 0;
    lv_font_t* custom_clock_font_ = nullptr;
    int32_t custom_clock_font_size_ = 0;

    bool has_custom_image() const;
    bool has_custom_clock_font() const;
    bool load_custom_font(int32_t font_size);
    void free_custom_font();
    const lv_font_t* resolve_clock_font(const String& layout, const String& font_choice);
    bool create_builtin(const String& style);
    bool create_clock();
    void update_builtin();
    void update_clock();
    static void animation_timer_callback(lv_timer_t* timer);
    bool load_image();
    void free_image();
};
