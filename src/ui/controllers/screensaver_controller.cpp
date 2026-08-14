#include "screensaver_controller.h"
#include "../../config/constants.h"
#include "../../config/logging.h"
#include "../../system/screensaver_settings.h"
#include <algorithm>
#include <cstring>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

ScreensaverController::ScreensaverController()
    : overlay_screen_(nullptr)
    , previous_screen_(nullptr)
    , image_widget_(nullptr)
    , primary_label_(nullptr)
    , secondary_label_(nullptr)
    , animation_timer_(nullptr)
    , image_buffer_(nullptr)
    , animation_step_(0)
    , visible_(false) {
    memset(&image_dsc_, 0, sizeof(image_dsc_));
}

ScreensaverController::~ScreensaverController() {
    hide();
}

bool ScreensaverController::has_image() const {
    return selected_style() != "custom" || has_custom_image();
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
    if (style == "custom" || style == "minimal" || style == "orbit" || style == "blank") {
        return style;
    }
    return fallback;
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
    if (style_ == "custom" && !load_image()) return;

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

    if (style_ == "custom") {
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
    } else if (!create_builtin(style_)) {
        lv_obj_delete(overlay_screen_);
        overlay_screen_ = nullptr;
        free_image();
        return;
    }

    // Load the overlay screen
    lv_screen_load(overlay_screen_);
    visible_ = true;
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
    }

    free_image();
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
    if (controller) controller->update_builtin();
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
    } else {
        static const int8_t offsets[][2] = {{0, 0}, {4, 2}, {-3, 4}, {-4, -2}, {3, -4}, {1, 3}, {-2, -3}, {0, 0}};
        lv_obj_align(primary_label_, LV_ALIGN_CENTER, offsets[animation_step_][0], offsets[animation_step_][1]);
        if (secondary_label_) {
            lv_obj_align_to(secondary_label_, primary_label_, LV_ALIGN_OUT_BOTTOM_MID, 0, 22);
        }
    }
}

bool ScreensaverController::load_image() {
    image_buffer_ = (uint8_t*)heap_caps_malloc(BLE_IMAGE_EXPECTED_SIZE,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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

    return true;
}

void ScreensaverController::free_image() {
    if (image_buffer_) {
        heap_caps_free(image_buffer_);
        image_buffer_ = nullptr;
    }
}
