#include "screensaver_controller.h"
#include "../../config/constants.h"
#include "../../config/logging.h"
#include "../../system/screensaver_settings.h"
#include "../../network/gaggimate_status_client.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <time.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

ScreensaverController::ScreensaverController()
    : overlay_screen_(nullptr)
    , previous_screen_(nullptr)
    , image_widget_(nullptr)
    , primary_label_(nullptr)
    , secondary_label_(nullptr)
    , detail_label_(nullptr)
    , hours_label_(nullptr)
    , colon_label_(nullptr)
    , minutes_label_(nullptr)
    , animation_timer_(nullptr)
    , image_buffer_(nullptr)
    , animation_step_(0)
    , visible_(false) {
    memset(&image_dsc_, 0, sizeof(image_dsc_));
}

static bool s_image_dirty_ = false;

ScreensaverController::~ScreensaverController() {
    hide();
    free_image();
}

void ScreensaverController::invalidate_image_cache() {
    s_image_dirty_ = true;
}

bool ScreensaverController::has_image() const {
    const String style = selected_style();
    if (style == "custom" || style == "clock_custom") {
        return has_custom_image();
    }
    return true;
}

bool ScreensaverController::has_custom_image() const {
    File image = LittleFS.open(BLE_IMAGE_FILENAME, "r");
    const bool valid = image && image.size() == BLE_IMAGE_EXPECTED_SIZE;
    if (image) {
        image.close();
    }
    return valid;
}

String ScreensaverController::selected_style() const {
    const String fallback = has_custom_image() ? "custom" : "minimal";
    Preferences preferences;
    if (!preferences.begin("screensaver", true)) return fallback;
    const String style = preferences.getString("style", fallback);
    preferences.end();
    if (style == "custom" || style == "clock_custom") {
        return has_custom_image() ? style : fallback;
    }
    if (style == "minimal" || style == "orbit" ||
        style == "blank" || style == "gaggimate" ||
        style == "clock") {
        return style;
    }
    return fallback;
}

String ScreensaverController::selected_clock_position() const {
    Preferences preferences;
    if (!preferences.begin("screensaver", true)) return "center";
    const String pos = preferences.getString("clock_pos", "center");
    preferences.end();
    if (pos == "top" || pos == "bottom" || pos == "center") {
        return pos;
    }
    return "center";
}

String ScreensaverController::selected_clock_layout() const {
    Preferences preferences;
    if (!preferences.begin("screensaver", true)) return "horizontal";
    const String layout = preferences.getString("clock_layout", "horizontal");
    preferences.end();
    if (layout == "stacked" || layout == "horizontal") {
        return layout;
    }
    return "horizontal";
}

uint32_t ScreensaverController::selected_clock_color() const {
    Preferences preferences;
    if (!preferences.begin("screensaver", true)) return 0xFFFFFF;
    String hex = preferences.getString("clock_color", "#FFFFFF");
    preferences.end();
    if (hex.startsWith("#")) {
        hex = hex.substring(1);
    }
    if (hex.length() != 6) return 0xFFFFFF;
    char* endptr = nullptr;
    unsigned long val = strtoul(hex.c_str(), &endptr, 16);
    if (!endptr || *endptr != '\0') return 0xFFFFFF;
    return static_cast<uint32_t>(val);
}

String ScreensaverController::selected_clock_font() const {
    Preferences preferences;
    if (!preferences.begin("screensaver", true)) return "montserrat";
    const String font = preferences.getString("clock_font", "montserrat");
    preferences.end();
    if (font == "custom" && has_custom_clock_font()) {
        return "custom";
    }
    return "montserrat";
}

bool ScreensaverController::has_custom_clock_font() const {
    File font = LittleFS.open(CLOCK_FONT_FILENAME, "r");
    const bool valid = font && font.size() >= 16 && font.size() <= CLOCK_FONT_MAX_SIZE;
    if (font) {
        font.close();
    }
    return valid;
}

bool ScreensaverController::load_custom_font(int32_t font_size) {
    if (custom_clock_font_ && custom_clock_font_size_ == font_size) {
        return true;
    }
    free_custom_font();

    File f = LittleFS.open(CLOCK_FONT_FILENAME, "r");
    if (!f) return false;
    const size_t size = f.size();
    if (size < 16 || size > CLOCK_FONT_MAX_SIZE) {
        f.close();
        return false;
    }

#if defined(SMART_GRIND_SIM)
    clock_font_buffer_ = (uint8_t*)malloc(size);
#else
    clock_font_buffer_ = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!clock_font_buffer_) {
        clock_font_buffer_ = (uint8_t*)malloc(size);
    }
#endif
    if (!clock_font_buffer_) {
        f.close();
        LOG_BLE("Screensaver: Custom font allocation failed (%u bytes)\n", (unsigned)size);
        return false;
    }

    size_t total_read = 0;
    while (total_read < size) {
        size_t bytes = f.read(clock_font_buffer_ + total_read, size - total_read);
        if (bytes == 0) break;
        total_read += bytes;
    }
    f.close();

    if (total_read != size) {
        LOG_BLE("Screensaver: Font file size mismatch (%u != %u)\n", (unsigned)total_read, (unsigned)size);
        free_custom_font();
        return false;
    }
    clock_font_size_ = size;

#if LV_USE_TINY_TTF
    custom_clock_font_ = lv_tiny_ttf_create_data(clock_font_buffer_, clock_font_size_, font_size);
    if (!custom_clock_font_) {
        LOG_BLE("Screensaver: lv_tiny_ttf_create_data failed for size %d\n", (int)font_size);
        free_custom_font();
        return false;
    }
    custom_clock_font_size_ = font_size;
    return true;
#else
    free_custom_font();
    return false;
#endif
}

void ScreensaverController::free_custom_font() {
#if LV_USE_TINY_TTF
    if (custom_clock_font_) {
        lv_tiny_ttf_destroy(custom_clock_font_);
        custom_clock_font_ = nullptr;
    }
#endif
    custom_clock_font_size_ = 0;
    if (clock_font_buffer_) {
#if !defined(SMART_GRIND_SIM)
        heap_caps_free(clock_font_buffer_);
#else
        free(clock_font_buffer_);
#endif
        clock_font_buffer_ = nullptr;
    }
    clock_font_size_ = 0;
}

const lv_font_t* ScreensaverController::resolve_clock_font(const String& layout, const String& font_choice) {
    const int32_t target_size = (layout == "stacked") ? 160 : 80;
    if (font_choice == "custom") {
        if (load_custom_font(target_size) && custom_clock_font_) {
            return custom_clock_font_;
        }
    }
    return (layout == "stacked") ? &lv_font_montserrat_160 : &lv_font_montserrat_80;
}

bool ScreensaverController::is_startup_enabled() const {
    return ScreensaverSettings::is_startup_enabled();
}

bool ScreensaverController::is_sleep_enabled() const {
    return ScreensaverSettings::is_sleep_enabled();
}

uint32_t ScreensaverController::get_startup_timeout_ms() const {
    auto settings = ScreensaverSettings::load_timing();
    return static_cast<uint32_t>(settings.startup_timeout_s) * 1000U;
}

void ScreensaverController::show() {
    if (visible_) return;
    style_ = selected_style();
    if (style_ != "custom" && style_ != "clock_custom") {
        free_image();
    } else {
        if (!load_image()) return;
    }

    // Save the current active screen so we can restore it on hide().
    // Screens in this project are created once and never deleted, so
    // this pointer remains valid for the lifetime of the application.
    previous_screen_ = lv_scr_act();

    // Create a new screen for the overlay
    overlay_screen_ = lv_obj_create(nullptr);
    if (!overlay_screen_) {
        free_image();
        return;
    }
    lv_obj_set_style_bg_color(overlay_screen_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay_screen_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(overlay_screen_, LV_OBJ_FLAG_SCROLLABLE);

    if (style_ == "custom" || style_ == "clock_custom") {
        image_widget_ = lv_img_create(overlay_screen_);
        if (!image_widget_) {
            lv_obj_delete(overlay_screen_);
            overlay_screen_ = nullptr;
            free_image();
            return;
        }
        image_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        image_dsc_.header.w = HW_DISPLAY_WIDTH_PX;
        image_dsc_.header.h = HW_DISPLAY_HEIGHT_PX;
        image_dsc_.data_size = BLE_IMAGE_EXPECTED_SIZE;
        image_dsc_.data = image_buffer_;
        lv_img_set_src(image_widget_, &image_dsc_);
        lv_obj_center(image_widget_);
    }

    visible_ = true;

    if (style_ == "clock" || style_ == "clock_custom") {
        if (!create_clock()) {
            visible_ = false;
            lv_obj_delete(overlay_screen_);
            overlay_screen_ = nullptr;
            free_image();
            return;
        }
    } else if (style_ != "custom") {
        if (!create_builtin(style_)) {
            visible_ = false;
            lv_obj_delete(overlay_screen_);
            overlay_screen_ = nullptr;
            free_image();
            return;
        }
    }

    // Load the overlay screen atomically with all widgets fully prepared
    lv_screen_load(overlay_screen_);
}

void ScreensaverController::hide() {
    if (!visible_) return;

    // Restore the previous active screen before deleting the overlay.
    if (previous_screen_) {
        lv_screen_load(previous_screen_);
        previous_screen_ = nullptr;
    }

    if (animation_timer_) {
        lv_timer_delete(animation_timer_);
        animation_timer_ = nullptr;
    }
    if (overlay_screen_) {
        lv_obj_delete(overlay_screen_);
        overlay_screen_ = nullptr;
        image_widget_ = nullptr;
        primary_label_ = nullptr;
        secondary_label_ = nullptr;
        detail_label_ = nullptr;
        hours_label_ = nullptr;
        colon_label_ = nullptr;
        minutes_label_ = nullptr;
    }

    if (style_ != "custom" && style_ != "clock_custom") {
        free_image();
    }
    free_custom_font();
    visible_ = false;
}

bool ScreensaverController::create_builtin(const String& style) {
    if (style == "blank") return true;

    primary_label_ = lv_label_create(overlay_screen_);
    if (!primary_label_) return false;
    lv_obj_set_style_text_color(primary_label_, lv_color_hex(0xE8EEE2), 0);
    lv_obj_set_style_text_align(primary_label_, LV_TEXT_ALIGN_CENTER, 0);

    if (style == "orbit") {
        lv_label_set_text(primary_label_, "SG");
        lv_obj_set_style_text_font(primary_label_, &lv_font_montserrat_32, 0);
        lv_obj_align(primary_label_, LV_ALIGN_TOP_LEFT, 22, 22);
        animation_timer_ = lv_timer_create(animation_timer_callback, 2500, this);
        return animation_timer_ != nullptr;
    }

    if (style == "gaggimate") {
        lv_label_set_text(primary_label_, "--.- C");
        lv_obj_set_style_text_font(primary_label_, &lv_font_montserrat_32, 0);
        lv_obj_align(primary_label_, LV_ALIGN_CENTER, 0, -46);

        secondary_label_ = lv_label_create(overlay_screen_);
        if (!secondary_label_) return false;
        lv_label_set_text(secondary_label_, "GAGGIMATE\nConnecting...");
        lv_obj_set_style_text_font(secondary_label_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(secondary_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(secondary_label_, lv_color_hex(THEME_COLOR_PRIMARY), 0);
        lv_obj_align(secondary_label_, LV_ALIGN_CENTER, 0, 14);

        detail_label_ = lv_label_create(overlay_screen_);
        if (!detail_label_) return false;
        lv_label_set_text(detail_label_, "Target --.- C");
        lv_obj_set_style_text_font(detail_label_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(detail_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(detail_label_, lv_color_hex(0xAAB5A6), 0);
        lv_obj_align(detail_label_, LV_ALIGN_CENTER, 0, 68);

        animation_timer_ = lv_timer_create(animation_timer_callback, 1000, this);
        return animation_timer_ != nullptr;
    }

    lv_label_set_text(primary_label_, "SMART\nGRIND");
    // Reuse the 32 px font already linked by the UI. Pulling in a second large
    // glyph set for a screensaver costs significant OTA partition headroom.
    lv_obj_set_style_text_font(primary_label_, &lv_font_montserrat_32, 0);
    lv_obj_center(primary_label_);
    secondary_label_ = lv_label_create(overlay_screen_);
    if (!secondary_label_) return false;
    lv_label_set_text(secondary_label_, "GRIND BY WEIGHT");
    lv_obj_set_style_text_font(secondary_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(secondary_label_, lv_color_hex(THEME_COLOR_PRIMARY), 0);
    lv_obj_align_to(secondary_label_, primary_label_, LV_ALIGN_OUT_BOTTOM_MID, 0, 22);
    animation_timer_ = lv_timer_create(animation_timer_callback, 12000, this);
    return animation_timer_ != nullptr;
}

void ScreensaverController::animation_timer_callback(lv_timer_t* timer) {
    auto* controller = static_cast<ScreensaverController*>(lv_timer_get_user_data(timer));
    if (!controller) return;
    if (controller->style_ == "clock" || controller->style_ == "clock_custom") {
        controller->update_clock();
    } else {
        controller->update_builtin();
    }
}

void ScreensaverController::update_builtin() {
    if (!visible_ || !primary_label_) return;
    animation_step_ = static_cast<uint8_t>((animation_step_ + 1) % 8);
    if (style_ == "orbit") {
        static const lv_align_t alignments[] = {
            LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_MID, LV_ALIGN_TOP_RIGHT, LV_ALIGN_RIGHT_MID,
            LV_ALIGN_BOTTOM_RIGHT, LV_ALIGN_BOTTOM_MID, LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_LEFT_MID
        };
        static const int16_t x_offsets[] = {22, 0, -22, -22, -22, 0, 22, 22};
        static const int16_t y_offsets[] = {22, 22, 22, 0, -22, -22, -22, 0};
        lv_obj_align(primary_label_, alignments[animation_step_], x_offsets[animation_step_], y_offsets[animation_step_]);
    } else if (style_ == "gaggimate") {
        const GaggiMateStatus status = gaggimate_status_client.status();
        const int8_t shift_x = static_cast<int8_t>((animation_step_ % 3) - 1) * 3;
        const int8_t shift_y = static_cast<int8_t>(((animation_step_ / 2) % 3) - 1) * 2;

        char primary[32];
        char secondary[64];
        char detail[96];
        if (!status.online) {
            snprintf(primary, sizeof(primary), "--.- C");
            snprintf(secondary, sizeof(secondary), "GAGGIMATE\nOffline");
            snprintf(detail, sizeof(detail), "Check Wi-Fi or host");
        } else if (status.active) {
            snprintf(primary, sizeof(primary), "%lu.%lus",
                     static_cast<unsigned long>(status.elapsed_ms / 1000),
                     static_cast<unsigned long>((status.elapsed_ms % 1000) / 100));
            snprintf(secondary, sizeof(secondary), "GAGGIMATE\n%s", status.phase);
            snprintf(detail, sizeof(detail), "%.1f bar  %.1f ml/s\n%.1f C",
                     status.pressure, status.flow, status.current_temperature);
        } else {
            const bool ready = status.target_temperature > 0.0f &&
                               fabsf(status.current_temperature - status.target_temperature) <= 1.0f;
            snprintf(primary, sizeof(primary), "%.1f C", status.current_temperature);
            snprintf(secondary, sizeof(secondary), "GAGGIMATE\n%s", ready ? "Ready" : "Heating");
            if (status.profile[0] != '\0') {
                snprintf(detail, sizeof(detail), "Target %.1f C\n%s",
                         status.target_temperature, status.profile);
            } else {
                snprintf(detail, sizeof(detail), "Target %.1f C", status.target_temperature);
            }
        }
        lv_label_set_text(primary_label_, primary);
        lv_label_set_text(secondary_label_, secondary);
        lv_label_set_text(detail_label_, detail);
        lv_obj_align(primary_label_, LV_ALIGN_CENTER, shift_x, -46 + shift_y);
        lv_obj_align(secondary_label_, LV_ALIGN_CENTER, shift_x, 14 + shift_y);
        lv_obj_align(detail_label_, LV_ALIGN_CENTER, shift_x, 68 + shift_y);
    } else {
        static const int8_t offsets[][2] = {{0, 0}, {4, 2}, {-3, 4}, {-4, -2}, {3, -4}, {1, 3}, {-2, -3}, {0, 0}};
        lv_obj_align(primary_label_, LV_ALIGN_CENTER, offsets[animation_step_][0], offsets[animation_step_][1]);
        if (secondary_label_) {
            lv_obj_align_to(secondary_label_, primary_label_, LV_ALIGN_OUT_BOTTOM_MID, 0, 22);
        }
    }
}

bool ScreensaverController::load_image() {
    if (image_buffer_ != nullptr && !s_image_dirty_) {
        return true;
    }
    if (image_buffer_ != nullptr && s_image_dirty_) {
        free_image();
        s_image_dirty_ = false;
    }

#if defined(SMART_GRIND_SIM)
    image_buffer_ = (uint8_t*)malloc(BLE_IMAGE_EXPECTED_SIZE);
#else
    image_buffer_ = (uint8_t*)heap_caps_malloc(BLE_IMAGE_EXPECTED_SIZE,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!image_buffer_) {
        image_buffer_ = (uint8_t*)malloc(BLE_IMAGE_EXPECTED_SIZE);
    }
#endif
    if (!image_buffer_) {
        LOG_BLE("Screensaver: PSRAM allocation failed (%d bytes)\n", BLE_IMAGE_EXPECTED_SIZE);
        return false;
    }

    File f = LittleFS.open(BLE_IMAGE_FILENAME, "r");
    if (!f) {
        LOG_BLE("Screensaver: Failed to open image file\n");
        free_image();
        return false;
    }

    size_t total_read = 0;
    const size_t chunk_size = 4096;
    while (total_read < BLE_IMAGE_EXPECTED_SIZE) {
        size_t to_read = std::min(chunk_size, (size_t)(BLE_IMAGE_EXPECTED_SIZE - total_read));
        size_t bytes_read = f.read(image_buffer_ + total_read, to_read);
        if (bytes_read == 0) break;
        total_read += bytes_read;
    }
    f.close();

    if (total_read != BLE_IMAGE_EXPECTED_SIZE) {
        LOG_BLE("Screensaver: Image file size mismatch (%u != %d)\n",
                (unsigned)total_read, BLE_IMAGE_EXPECTED_SIZE);
        free_image();
        return false;
    }

    s_image_dirty_ = false;
    return true;
}

void ScreensaverController::free_image() {
    if (image_buffer_) {
#if !defined(SMART_GRIND_SIM)
        heap_caps_free(image_buffer_);
#else
        free(image_buffer_);
#endif
        image_buffer_ = nullptr;
    }
}

bool ScreensaverController::create_clock() {
    colon_label_ = lv_label_create(overlay_screen_);
    if (!colon_label_) return false;
    hours_label_ = lv_label_create(overlay_screen_);
    if (!hours_label_) return false;
    minutes_label_ = lv_label_create(overlay_screen_);
    if (!minutes_label_) return false;

    const String layout = selected_clock_layout();
    clock_font_choice_ = selected_clock_font();
    const lv_font_t* font = resolve_clock_font(layout, clock_font_choice_);

    clock_color_ = selected_clock_color();
    lv_obj_t* labels[] = {colon_label_, hours_label_, minutes_label_};
    for (lv_obj_t* lbl : labels) {
        lv_obj_set_style_text_font(lbl, font, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(clock_color_), 0);
    }

    if (layout == "stacked") {
        lv_obj_set_style_text_align(hours_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(minutes_label_, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        lv_obj_set_style_text_align(colon_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(hours_label_, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_align(minutes_label_, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(colon_label_, ":");
    }

    lv_label_set_text(hours_label_, "");
    lv_label_set_text(minutes_label_, "");

    // Initially hide until time sync is verified
    lv_obj_add_flag(colon_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hours_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(minutes_label_, LV_OBJ_FLAG_HIDDEN);

    update_clock();

    animation_timer_ = lv_timer_create(animation_timer_callback, 1000, this);
    return animation_timer_ != nullptr;
}

void ScreensaverController::update_clock() {
    if (!visible_ || !colon_label_ || !hours_label_ || !minutes_label_) return;

    time_t now = time(nullptr);
    if (now < 1700000000) {
        lv_obj_add_flag(colon_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hours_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(minutes_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    struct tm timeinfo;
    if (!localtime_r(&now, &timeinfo)) {
        lv_obj_add_flag(colon_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hours_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(minutes_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    char h_str[4];
    char m_str[4];
    snprintf(h_str, sizeof(h_str), "%02d", timeinfo.tm_hour);
    snprintf(m_str, sizeof(m_str), "%02d", timeinfo.tm_min);
    lv_label_set_text(hours_label_, h_str);
    lv_label_set_text(minutes_label_, m_str);

    const String layout = selected_clock_layout();
    const String pos = selected_clock_position();
    const String font_choice = selected_clock_font();

    const lv_font_t* font = resolve_clock_font(layout, font_choice);
    clock_font_choice_ = font_choice;
    if (lv_obj_get_style_text_font(hours_label_, LV_PART_MAIN) != font) {
        lv_obj_set_style_text_font(colon_label_, font, 0);
        lv_obj_set_style_text_font(hours_label_, font, 0);
        lv_obj_set_style_text_font(minutes_label_, font, 0);
        if (layout == "stacked") {
            lv_obj_set_style_text_align(hours_label_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_align(minutes_label_, LV_TEXT_ALIGN_CENTER, 0);
        } else {
            lv_obj_set_style_text_align(colon_label_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_align(hours_label_, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_text_align(minutes_label_, LV_TEXT_ALIGN_LEFT, 0);
            lv_label_set_text(colon_label_, ":");
        }
    }

    const uint32_t new_color = selected_clock_color();
    if (new_color != clock_color_) {
        clock_color_ = new_color;
        const lv_color_t color = lv_color_hex(clock_color_);
        lv_obj_set_style_text_color(colon_label_, color, 0);
        lv_obj_set_style_text_color(hours_label_, color, 0);
        lv_obj_set_style_text_color(minutes_label_, color, 0);
    }

    // Subtle burn-in shift based on current minute (orbit pattern ±2 px)
    static const int8_t shift_x[] = {0, 2, -2, 1, -1, 2, -2, 0};
    static const int8_t shift_y[] = {0, 1, -1, 2, -2, -1, 1, 0};
    const uint8_t idx = static_cast<uint8_t>(timeinfo.tm_min % 8);

    if (layout == "stacked") {
        lv_obj_add_flag(colon_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(hours_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(minutes_label_, LV_OBJ_FLAG_HIDDEN);

        int16_t hours_y = -70;
        int16_t minutes_y = 70;
        if (pos == "top") {
            hours_y = -140;
            minutes_y = 0;
        } else if (pos == "bottom") {
            hours_y = 0;
            minutes_y = 140;
        }

        lv_obj_align(hours_label_, LV_ALIGN_CENTER, shift_x[idx], hours_y + shift_y[idx]);
        lv_obj_align(minutes_label_, LV_ALIGN_CENTER, shift_x[idx], minutes_y + shift_y[idx]);
    } else {
        lv_obj_remove_flag(colon_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(hours_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(minutes_label_, LV_OBJ_FLAG_HIDDEN);

        int16_t base_y = 0;
        if (pos == "top") {
            base_y = -130;
        } else if (pos == "bottom") {
            base_y = 130;
        }

        lv_obj_align(colon_label_, LV_ALIGN_CENTER, shift_x[idx], base_y + shift_y[idx]);

        lv_obj_update_layout(colon_label_);
        lv_obj_update_layout(hours_label_);
        lv_obj_update_layout(minutes_label_);

        lv_obj_align_to(hours_label_, colon_label_, LV_ALIGN_OUT_LEFT_MID, -2, 0);
        lv_obj_align_to(minutes_label_, colon_label_, LV_ALIGN_OUT_RIGHT_MID, 2, 0);
    }
}
