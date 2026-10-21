#include "grinding_screen_disc.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <Arduino.h>
#include "../../config/constants.h"

static constexpr int DISC_MIN_DIAMETER_PX = 24;
static constexpr int DISC_MAX_DIAMETER_PX = 600;
static constexpr int DISC_CENTER_Y_OFFSET = 14;
static constexpr uint32_t COLOR_COMPLETE_GREEN = 0x00C853;  // Vibrant emerald green upon completion

const DiscColorPresetDef DISC_COLOR_PRESETS[NUM_DISC_COLOR_PRESETS] = {
    {"emerald",  "Emerald",  0x1B6B48},
    {"charcoal", "Charcoal", 0x32363C},
    {"espresso", "Espresso", 0x52321B},
    {"midnight", "Midnight", 0x183054},
    {"burgundy", "Burgundy", 0x541822}
};

GrindingScreenDisc::GrindingScreenDisc()
    : screen(nullptr)
    , profile_label(nullptr)
    , target_label(nullptr)
    , disc_obj(nullptr)
    , accent_ring(nullptr)
    , weight_label(nullptr)
    , visible(false)
    , time_mode(false)
    , manual_mode(false)
    , completed_(false)
    , displayed_progress(-1)
    , target_time_seconds_(0.0f)
    , color_preset_index_(0)
    , active_color_(DISC_COLOR_PRESETS[0].color_hex)
    , finish_style_(DiscFinishStyle::ACCENT_RING) {
    displayed_weight_text[0] = '\0';
}

GrindingScreenDisc::~GrindingScreenDisc() {
}

void GrindingScreenDisc::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);

    // Expanding circular disc (created first and kept in background behind text labels)
    disc_obj = lv_obj_create(screen);
    lv_obj_set_size(disc_obj, DISC_MIN_DIAMETER_PX, DISC_MIN_DIAMETER_PX);
    lv_obj_align(disc_obj, LV_ALIGN_CENTER, 0, DISC_CENTER_Y_OFFSET);
    lv_obj_set_style_radius(disc_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc_obj, lv_color_hex(active_color_), 0);
    lv_obj_set_style_bg_opa(disc_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disc_obj, 0, 0);
    lv_obj_set_style_pad_all(disc_obj, 0, 0);
    lv_obj_clear_flag(disc_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_background(disc_obj);

    // Completion accent ring (Variant B: glowing emerald ring framing weight digits)
    accent_ring = lv_obj_create(screen);
    lv_obj_set_size(accent_ring, 200, 200);
    lv_obj_align(accent_ring, LV_ALIGN_CENTER, 0, DISC_CENTER_Y_OFFSET);
    lv_obj_set_style_radius(accent_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(accent_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(accent_ring, lv_color_hex(COLOR_COMPLETE_GREEN), 0);
    lv_obj_set_style_border_width(accent_ring, 3, 0);
    lv_obj_set_style_border_opa(accent_ring, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(accent_ring, 0, 0);
    lv_obj_clear_flag(accent_ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(accent_ring, LV_OBJ_FLAG_HIDDEN);

    // Profile label
    profile_label = lv_label_create(screen);
    lv_label_set_text(profile_label, "DOUBLE");
    lv_obj_set_style_text_font(profile_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(profile_label, lv_color_hex(THEME_COLOR_SECONDARY), 0);
    lv_obj_align(profile_label, LV_ALIGN_TOP_MID, 0, 20);

    // Target label (bright high-contrast text)
    target_label = lv_label_create(screen);
    lv_label_set_text(target_label, "Target: 18.0g");
    lv_obj_set_style_text_font(target_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(target_label, lv_color_hex(0xE0E0E0), 0);
    lv_obj_align(target_label, LV_ALIGN_TOP_MID, 0, 56);

    // Weight label centered over the disc
    weight_label = lv_label_create(screen);
    lv_label_set_text(weight_label, "0.0g");
    lv_obj_set_style_text_font(weight_label, &lv_font_montserrat_56, 0);
    lv_obj_set_style_text_color(weight_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(weight_label, LV_ALIGN_CENTER, 0, DISC_CENTER_Y_OFFSET);

    // Forward clicks from all children to parent screen
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(screen); i++) {
        lv_obj_clear_flag(lv_obj_get_child(screen, i), LV_OBJ_FLAG_CLICKABLE);
    }

    visible = false;
    time_mode = false;
    completed_ = false;
    target_time_seconds_ = 0.0f;
    std::snprintf(displayed_weight_text, sizeof(displayed_weight_text), "0.0g");
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void GrindingScreenDisc::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    if (disc_obj && !completed_ && !manual_mode) {
        lv_obj_clear_flag(disc_obj, LV_OBJ_FLAG_HIDDEN);
    }
    visible = true;
}

void GrindingScreenDisc::hide() {
    if (accent_ring) {
        lv_obj_add_flag(accent_ring, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void GrindingScreenDisc::update_profile_name(const char* name) {
    if (profile_label) {
        lv_label_set_text(profile_label, name);
    }
}

void GrindingScreenDisc::update_target_weight(float weight) {
    if (time_mode || !target_label) return;
    char target_text[32];
    snprintf(target_text, sizeof(target_text), "Target: " SYS_WEIGHT_DISPLAY_FORMAT, weight);
    lv_label_set_text(target_label, target_text);
}

void GrindingScreenDisc::update_target_weight_text(const char* text) {
    if (target_label) {
        lv_label_set_text(target_label, text);
    }
}

void GrindingScreenDisc::update_target_time(float seconds) {
    target_time_seconds_ = seconds;
    if (!target_label) return;
    char target_text[32];
    snprintf(target_text, sizeof(target_text), "Time: %.1fs", seconds);
    lv_label_set_text(target_label, target_text);
}

void GrindingScreenDisc::update_current_weight(float weight) {
    if (time_mode || !weight_label) return;
    char weight_text[16];
    snprintf(weight_text, sizeof(weight_text), SYS_WEIGHT_DISPLAY_FORMAT, weight);
    if (std::strcmp(displayed_weight_text, weight_text) == 0) {
        return;
    }
    std::snprintf(displayed_weight_text, sizeof(displayed_weight_text), "%s", weight_text);
    lv_label_set_text(weight_label, weight_text);
}

void GrindingScreenDisc::update_tare_display() {
    std::snprintf(displayed_weight_text, sizeof(displayed_weight_text), "TARE");
    if (weight_label) lv_label_set_text(weight_label, "TARE");
    displayed_progress = -1;
    if (accent_ring) lv_obj_add_flag(accent_ring, LV_OBJ_FLAG_HIDDEN);
    if (disc_obj) {
        if (!manual_mode && !completed_) {
            lv_obj_clear_flag(disc_obj, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_size(disc_obj, DISC_MIN_DIAMETER_PX, DISC_MIN_DIAMETER_PX);
        lv_obj_align(disc_obj, LV_ALIGN_CENTER, 0, DISC_CENTER_Y_OFFSET);
    }
}

void GrindingScreenDisc::update_progress(int percent) {
    if (manual_mode) {
        return;
    }
    int clamped = std::clamp(percent, 0, 100);
    if (displayed_progress == clamped) {
        return;
    }
    displayed_progress = clamped;

    if (!completed_ && disc_obj) {
        lv_obj_clear_flag(disc_obj, LV_OBJ_FLAG_HIDDEN);
        int diameter = DISC_MIN_DIAMETER_PX + (clamped * (DISC_MAX_DIAMETER_PX - DISC_MIN_DIAMETER_PX)) / 100;
        lv_obj_set_size(disc_obj, diameter, diameter);
        lv_obj_align(disc_obj, LV_ALIGN_CENTER, 0, DISC_CENTER_Y_OFFSET);
    }

    if (time_mode && target_time_seconds_ > 0.0f && weight_label) {
        float elapsed_s = (clamped / 100.0f) * target_time_seconds_;
        char elapsed_text[16];
        snprintf(elapsed_text, sizeof(elapsed_text), "%.1fs", elapsed_s);
        lv_label_set_text(weight_label, elapsed_text);
    }
}

void GrindingScreenDisc::set_completed(bool completed) {
    if (completed_ == completed) return;
    completed_ = completed;

    if (!screen) return;

    if (manual_mode) {
        if (accent_ring) lv_obj_add_flag(accent_ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
        if (disc_obj) lv_obj_add_flag(disc_obj, LV_OBJ_FLAG_HIDDEN);
        if (target_label) lv_obj_add_flag(target_label, LV_OBJ_FLAG_HIDDEN);
        if (weight_label) {
            lv_obj_align(weight_label, LV_ALIGN_CENTER, 0, DISC_CENTER_Y_OFFSET);
            lv_obj_set_style_text_color(weight_label,
                completed_ ? lv_color_hex(COLOR_COMPLETE_GREEN) : lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
        }
        if (profile_label) lv_obj_set_style_text_color(profile_label, lv_color_hex(THEME_COLOR_SECONDARY), 0);
        return;
    }

    if (completed_) {
        if (finish_style_ == DiscFinishStyle::SOLID_GREEN) {
            // Option 1 (Solid): Full-screen solid green card (instant 1-frame switch)
            lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_COMPLETE_GREEN), 0);
            lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

            if (disc_obj) lv_obj_add_flag(disc_obj, LV_OBJ_FLAG_HIDDEN);
            if (accent_ring) lv_obj_add_flag(accent_ring, LV_OBJ_FLAG_HIDDEN);
            if (profile_label) lv_obj_set_style_text_color(profile_label, lv_color_hex(0xFFFFFF), 0);
            if (target_label) lv_obj_set_style_text_color(target_label, lv_color_hex(0xE0FFE8), 0);
            if (weight_label) lv_obj_set_style_text_color(weight_label, lv_color_hex(0xFFFFFF), 0);
        } else {
            // Option 2 (Ring): Background stays the chosen disc preset color,
            // while the weight digits and accent ring illuminate in emerald green.
            lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);

            if (disc_obj) {
                lv_obj_clear_flag(disc_obj, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_size(disc_obj, DISC_MAX_DIAMETER_PX, DISC_MAX_DIAMETER_PX);
                lv_obj_align(disc_obj, LV_ALIGN_CENTER, 0, DISC_CENTER_Y_OFFSET);
                lv_obj_set_style_bg_color(disc_obj, lv_color_hex(active_color_), 0);
            }
            if (accent_ring) {
                lv_obj_clear_flag(accent_ring, LV_OBJ_FLAG_HIDDEN);
            }
            if (profile_label) lv_obj_set_style_text_color(profile_label, lv_color_hex(0xFFFFFF), 0);
            if (target_label) lv_obj_set_style_text_color(target_label, lv_color_hex(0x69F0AE), 0);
            if (weight_label) lv_obj_set_style_text_color(weight_label, lv_color_hex(COLOR_COMPLETE_GREEN), 0);
        }
    } else {
        // Normal active grinding display
        lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);

        if (accent_ring) {
            lv_obj_add_flag(accent_ring, LV_OBJ_FLAG_HIDDEN);
        }
        if (disc_obj) {
            lv_obj_clear_flag(disc_obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(disc_obj, lv_color_hex(active_color_), 0);
            int diameter = DISC_MIN_DIAMETER_PX;
            if (displayed_progress > 0) {
                diameter += (displayed_progress * (DISC_MAX_DIAMETER_PX - DISC_MIN_DIAMETER_PX)) / 100;
            }
            lv_obj_set_size(disc_obj, diameter, diameter);
            lv_obj_align(disc_obj, LV_ALIGN_CENTER, 0, DISC_CENTER_Y_OFFSET);
        }
        if (profile_label) lv_obj_set_style_text_color(profile_label, lv_color_hex(THEME_COLOR_SECONDARY), 0);
        if (target_label) lv_obj_set_style_text_color(target_label, lv_color_hex(0xE0E0E0), 0);
        if (weight_label) lv_obj_set_style_text_color(weight_label, lv_color_hex(0xFFFFFF), 0);
    }
}

void GrindingScreenDisc::set_time_mode(bool enabled) {
    time_mode = enabled;
}

void GrindingScreenDisc::set_manual_mode(bool enabled) {
    manual_mode = enabled;
    if (manual_mode) {
        if (accent_ring) lv_obj_add_flag(accent_ring, LV_OBJ_FLAG_HIDDEN);
        if (disc_obj) lv_obj_add_flag(disc_obj, LV_OBJ_FLAG_HIDDEN);
        if (target_label) lv_obj_add_flag(target_label, LV_OBJ_FLAG_HIDDEN);
        if (weight_label) lv_obj_align(weight_label, LV_ALIGN_CENTER, 0, DISC_CENTER_Y_OFFSET);
    } else {
        if (disc_obj && !completed_) lv_obj_clear_flag(disc_obj, LV_OBJ_FLAG_HIDDEN);
        if (accent_ring && completed_) lv_obj_clear_flag(accent_ring, LV_OBJ_FLAG_HIDDEN);
        if (target_label) lv_obj_clear_flag(target_label, LV_OBJ_FLAG_HIDDEN);
        if (weight_label) lv_obj_align(weight_label, LV_ALIGN_CENTER, 0, DISC_CENTER_Y_OFFSET);
    }
}

void GrindingScreenDisc::set_color_preset(int preset_index) {
    if (preset_index < 0 || preset_index >= NUM_DISC_COLOR_PRESETS) {
        preset_index = 0;
    }
    color_preset_index_ = preset_index;
    active_color_ = DISC_COLOR_PRESETS[preset_index].color_hex;
    if (disc_obj) {
        lv_obj_set_style_bg_color(disc_obj, lv_color_hex(active_color_), 0);
    }
}

const DiscColorPresetDef& GrindingScreenDisc::get_color_preset_def() const {
    if (color_preset_index_ < 0 || color_preset_index_ >= NUM_DISC_COLOR_PRESETS) {
        return DISC_COLOR_PRESETS[0];
    }
    return DISC_COLOR_PRESETS[color_preset_index_];
}
