#include "grinding_screen_circle.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// The circle grows evenly from the middle of the display. Positions are from
// the top of the display.
constexpr int32_t kCircleCenterY = HW_DISPLAY_HEIGHT_PX / 2;
constexpr int32_t kCircleCenterOffsetY = kCircleCenterY - HW_DISPLAY_HEIGHT_PX / 2;
constexpr int32_t kProfileTopY = kCircleCenterY - 77;
constexpr int32_t kTargetTopY = kCircleCenterY + 40;
constexpr int32_t kTargetWidthPx = 210;

// Progress arrives in 1% steps a few times a second; easing between them
// keeps the circle growing smoothly instead of jumping.
constexpr uint32_t kGrowAnimationMs = 160;


// Share of the display covered by a circle of this radius centred on it.
float visible_share(float radius) {
    const float half_width = HW_DISPLAY_WIDTH_PX / 2.0f;
    float area = 0.0f;
    for (int32_t row = 0; row < HW_DISPLAY_HEIGHT_PX; ++row) {
        const float dy = row + 0.5f - HW_DISPLAY_HEIGHT_PX / 2.0f;
        if (std::fabs(dy) >= radius) continue;
        area += 2.0f * std::min(std::sqrt(radius * radius - dy * dy), half_width);
    }
    return area / (static_cast<float>(HW_DISPLAY_WIDTH_PX) * HW_DISPLAY_HEIGHT_PX);
}

}  // namespace

void GrindingScreenCircle::build_diameter_table() {
    // Growing the diameter at a constant rate would cover this narrow display
    // long before the target (95% of it at three quarters). Instead choose the
    // diameter by how much of the display it covers: start from the share the
    // text circle needs and ease in (progress^1.5) so the covered share stays
    // close to the share of the target ground, filling only at the target.
    const int32_t min_radius = THEME_GRIND_CIRCLE_MIN_DIAMETER_PX / 2;
    const int32_t max_radius = THEME_GRIND_CIRCLE_MAX_DIAMETER_PX / 2;
    const float start_share = visible_share(min_radius);
    int32_t radius = min_radius;
    for (int percent = 0; percent <= 100; ++percent) {
        const float progress = percent / 100.0f;
        const float target_share =
            start_share + (1.0f - start_share) * progress * std::sqrt(progress);
        while (radius < max_radius && visible_share(radius) < target_share) {
            ++radius;
        }
        diameter_by_percent[percent] = static_cast<int16_t>(2 * radius);
    }
    diameter_by_percent[100] = THEME_GRIND_CIRCLE_MAX_DIAMETER_PX;
}

int32_t GrindingScreenCircle::diameter_for_progress(int percent) const {
    return diameter_by_percent[std::clamp(percent, 0, 100)];
}

void GrindingScreenCircle::create() {
    build_diameter_table();

    // Full display so the finished colour covers everything behind the
    // grind button. The theme's padding, border and scrollbars are not needed.
    screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    // The circle grows past the display edges; that must not make it scroll.
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE); // Tapping switches layout

    circle = lv_obj_create(screen);
    lv_obj_remove_style_all(circle);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, kCircleCenterOffsetY);
    set_circle_diameter(circle, diameter_for_progress(0));

    profile_label = create_label(&lv_font_montserrat_24);
    lv_label_set_text(profile_label, "DOUBLE");
    lv_obj_align(profile_label, LV_ALIGN_TOP_MID, 0, kProfileTopY);

    weight_label = create_label(&lv_font_montserrat_56);
    lv_label_set_text(weight_label, "0.0g");
    lv_obj_align(weight_label, LV_ALIGN_CENTER, 0, kCircleCenterOffsetY);
    std::snprintf(displayed_weight_text, sizeof(displayed_weight_text), "0.0g");

    target_label = create_label(&lv_font_montserrat_24);
    lv_label_set_text(target_label, "Target: 18.0g");
    lv_obj_set_width(target_label, kTargetWidthPx);
    lv_label_set_long_mode(target_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(target_label, LV_ALIGN_TOP_MID, 0, kTargetTopY);

    displayed_progress = 0;
    outcome = GrindScreenOutcome::IN_PROGRESS;
    corner_color = lv_color_hex(THEME_COLOR_BACKGROUND);
    apply_colors(false);

    visible = false;
    time_mode = false;
    target_time_seconds_ = 0.0f;
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* GrindingScreenCircle::create_label(const lv_font_t* font) {
    lv_obj_t* label = lv_label_create(screen);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

void GrindingScreenCircle::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void GrindingScreenCircle::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void GrindingScreenCircle::update_profile_name(const char* name) {
    lv_label_set_text(profile_label, name);
}

void GrindingScreenCircle::update_target_weight(float weight) {
    if (time_mode) {
        return;
    }
    char target_text[32];
    snprintf(target_text, sizeof(target_text), "Target: " SYS_WEIGHT_DISPLAY_FORMAT, weight);
    lv_label_set_text(target_label, target_text);
}

void GrindingScreenCircle::update_target_weight_text(const char* text) {
    lv_label_set_text(target_label, text);
}

void GrindingScreenCircle::update_target_time(float seconds) {
    target_time_seconds_ = seconds;
    char target_text[32];
    snprintf(target_text, sizeof(target_text), "Time: %.1fs", seconds);
    lv_label_set_text(target_label, target_text);
}

void GrindingScreenCircle::update_current_weight(float weight) {
    char weight_text[16];
    snprintf(weight_text, sizeof(weight_text), SYS_WEIGHT_DISPLAY_FORMAT, weight);
    if (std::strcmp(displayed_weight_text, weight_text) == 0) {
        return;
    }
    std::snprintf(displayed_weight_text, sizeof(displayed_weight_text), "%s", weight_text);
    lv_label_set_text(weight_label, weight_text);
}

void GrindingScreenCircle::update_tare_display() {
    std::snprintf(displayed_weight_text, sizeof(displayed_weight_text), "TARE");
    lv_label_set_text(weight_label, "TARE");
    displayed_progress = 0;
    resize_circle(diameter_for_progress(0), false);
}

void GrindingScreenCircle::update_progress(int percent) {
    if (displayed_progress == percent) {
        return;
    }
    displayed_progress = percent;
    // A finished grind keeps the display covered in its result colour.
    if (outcome == GrindScreenOutcome::IN_PROGRESS) {
        resize_circle(diameter_for_progress(percent), visible);
    }
    if (time_mode && target_time_seconds_ > 0.0f) {
        // Show elapsed time in the centre instead of the sensor weight
        float elapsed_s = (percent / 100.0f) * target_time_seconds_;
        char elapsed_text[16];
        snprintf(elapsed_text, sizeof(elapsed_text), "%.1fs", elapsed_s);
        lv_label_set_text(weight_label, elapsed_text);
    }
}

void GrindingScreenCircle::set_outcome(GrindScreenOutcome new_outcome) {
    if (new_outcome == GrindScreenOutcome::IN_PROGRESS) {
        outcome = new_outcome;
        // The corners show the display background again while grinding.
        lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
        apply_colors(false);
        // A new grind starts small without shrinking from the last result.
        displayed_progress = 0;
        resize_circle(diameter_for_progress(0), false);
        return;
    }
    // A time-mode pulse reports the same result again; keep showing it.
    if (new_outcome == outcome) {
        return;
    }
    outcome = new_outcome;
    show_result_colors();
}

void GrindingScreenCircle::set_time_mode(bool enabled) {
    time_mode = enabled;
}

// The whole display switches to the result colour in one frame. Every part is
// a solid colour, so the frame is quick to draw and send to the panel.
void GrindingScreenCircle::show_result_colors() {
    lv_anim_delete(circle, set_circle_diameter);
    // Paint the corners outside the circle too, so the display is one colour.
    corner_color = lv_obj_get_style_bg_color(lv_obj_get_parent(screen), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    apply_colors(true);
}

// Colours the circle, the corners and the text for the grind in progress or
// for its result.
void GrindingScreenCircle::apply_colors(bool show_result) {
    const bool complete = show_result && outcome == GrindScreenOutcome::COMPLETE;
    const uint32_t result = complete ? THEME_COLOR_SUCCESS : THEME_COLOR_WARNING;
    lv_obj_set_style_bg_color(circle, lv_color_hex(show_result ? result : THEME_COLOR_GRIND_CIRCLE), 0);
    lv_obj_set_style_bg_color(screen, show_result ? lv_color_hex(result) : corner_color, 0);

    // Dark text reads on the light circle and on amber; green needs white.
    lv_obj_set_style_text_color(
        weight_label, lv_color_hex(complete ? THEME_COLOR_TEXT_PRIMARY : THEME_COLOR_GRIND_CIRCLE_TEXT), 0);
    const lv_color_t secondary_text =
        lv_color_hex(complete ? THEME_COLOR_TEXT_PRIMARY : THEME_COLOR_GRIND_CIRCLE_TEXT_SECONDARY);
    lv_obj_set_style_text_color(profile_label, secondary_text, 0);
    lv_obj_set_style_text_color(target_label, secondary_text, 0);
}

void GrindingScreenCircle::resize_circle(int32_t diameter, bool animate) {
    lv_anim_delete(circle, set_circle_diameter);
    const int32_t current = lv_obj_get_style_width(circle, LV_PART_MAIN);
    if (!animate || current == diameter) {
        set_circle_diameter(circle, diameter);
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, circle);
    lv_anim_set_exec_cb(&anim, set_circle_diameter);
    lv_anim_set_values(&anim, current, diameter);
    lv_anim_set_duration(&anim, kGrowAnimationMs);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

void GrindingScreenCircle::set_circle_diameter(void* circle, int32_t diameter) {
    lv_obj_set_size(static_cast<lv_obj_t*>(circle), diameter, diameter);
}
