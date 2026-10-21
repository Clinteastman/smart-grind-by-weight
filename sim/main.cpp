#include <lvgl.h>
#include <src/drivers/windows/lv_windows_display.h>
#include <src/drivers/windows/lv_windows_input.h>

#include "config/constants.h"
#include "controllers/grind_mode.h"
#include "ui/screens/grinding_screen_arc.h"
#include "ui/screens/grinding_screen_chart.h"
#include "ui/screens/grinding_screen_disc.h"
#include "ui/screens/ready_screen.h"

#include <algorithm>
#include <cstdarg>
#include <csignal>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <windows.h>

namespace {

void sim_log(const char* fmt, ...) {
    static FILE* log_file = nullptr;
    if (!log_file) {
        fopen_s(&log_file, "sim_debug.log", "w");
    }
    if (log_file) {
        va_list args;
        va_start(args, fmt);
        vfprintf(log_file, fmt, args);
        va_end(args);
        fflush(log_file);
    }
}

constexpr int kDisplayWidth = 280;
constexpr int kDisplayHeight = 456;
constexpr float kTargetWeight = USER_SINGLE_ESPRESSO_WEIGHT_G;
constexpr uint64_t kArcPixelBudget = 800000;
constexpr uint64_t kChartPixelBudget = 3500000;
constexpr uint64_t kSwipePixelBudget = 1000000;
constexpr uint32_t kSwipeDurationBudgetMs = 300;

enum class SimGrindLayout { ARC = 0, DISC_RING = 1, DISC_SOLID = 2, CHART = 3, DISC = DISC_RING };

ReadyScreen ready_screen;
GrindingScreenArc arc_screen;
GrindingScreenChart chart_screen;
GrindingScreenDisc disc_screen;
lv_obj_t* grind_button = nullptr;
lv_obj_t* grind_icon = nullptr;
lv_obj_t* pulse_button = nullptr;
SimGrindLayout active_layout = SimGrindLayout::ARC;
GrindMode current_mode = GrindMode::WEIGHT;
const char* active_profile_name = "SINGLE";
float active_target = USER_SINGLE_ESPRESSO_WEIGHT_G;
bool grinding = false;
bool settling = false;
bool manual_grinding = false;
float weight_g = 0.0f;
float flow_gps = 0.0f;
uint32_t grind_started_ms = 0;
uint32_t last_update_ms = 0;
HWND simulator_window = nullptr;

bool menu_open = false;
lv_obj_t* sim_menu_screen = nullptr;
lv_obj_t* anim_btn_label = nullptr;
lv_obj_t* mode_btn_label = nullptr;
lv_obj_t* icons_btn_label = nullptr;
lv_obj_t* disc_color_btn = nullptr;
lv_obj_t* disc_color_btn_label = nullptr;
lv_obj_t* target_btn_label = nullptr;
float sim_single_target = USER_SINGLE_ESPRESSO_WEIGHT_G;
bool demo_mode = false;
int last_seen_tab = -1;

void update_menu_labels();
void cycle_single_target();
void toggle_mode_icons();
void cycle_disc_color();
void open_menu();
void close_menu();
void show_ready();
void update_grinder_controls_state();

struct RenderMetrics {
    uint64_t flushed_pixels = 0;
    uint64_t render_time_ms = 0;
    uint32_t flush_count = 0;
    uint32_t refresh_count = 0;
    uint32_t render_started_ms = 0;
};

RenderMetrics render_metrics;

uint32_t now_ms() {
    return static_cast<uint32_t>(GetTickCount64());
}

void reset_render_metrics() {
    render_metrics = {};
}

void print_render_metrics(const char* layout, uint32_t duration_ms) {
    std::printf(
        "[benchmark] %s duration=%lums refreshes=%lu flushes=%lu pixels=%llu render=%llums\n",
        layout,
        static_cast<unsigned long>(duration_ms),
        static_cast<unsigned long>(render_metrics.refresh_count),
        static_cast<unsigned long>(render_metrics.flush_count),
        static_cast<unsigned long long>(render_metrics.flushed_pixels),
        static_cast<unsigned long long>(render_metrics.render_time_ms));
    std::fflush(stdout);
}

void display_metrics_event(lv_event_t* event) {
    switch (lv_event_get_code(event)) {
        case LV_EVENT_RENDER_START:
            render_metrics.render_started_ms = now_ms();
            break;
        case LV_EVENT_RENDER_READY:
            if (render_metrics.render_started_ms != 0) {
                render_metrics.render_time_ms += now_ms() - render_metrics.render_started_ms;
                render_metrics.render_started_ms = 0;
            }
            break;
        case LV_EVENT_FLUSH_START: {
            const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event));
            if (area) {
                render_metrics.flushed_pixels += static_cast<uint64_t>(lv_area_get_size(area));
                render_metrics.flush_count++;
            }
            break;
        }
        case LV_EVENT_REFR_READY:
            render_metrics.refresh_count++;
            break;
        default:
            break;
    }
}

wchar_t g_pending_title[256] = L"";
bool g_title_dirty = false;

void set_status(const char* status) {
    if (status) {
        swprintf_s(
            g_pending_title,
            L"Smart Grind Simulator - %S  [Touch/Drag, Space: Grind, 1-6/Arrows: Tabs, M: Mode (%S), V: View, I: Icons/Text, C: Disc Color, T: Tare]",
            status,
            current_mode == GrindMode::TIME ? "Time" : "Weight");
        g_title_dirty = true;
    }
}

void set_grind_button(const char* symbol, uint32_t color) {
    if (grind_icon) {
        lv_image_set_src(grind_icon, symbol);
    }
    if (grind_button) {
        lv_obj_set_style_bg_color(grind_button, lv_color_hex(color), 0);
    }
}

void update_ready_screen_profiles() {
    const float values[3] = {
        current_mode == GrindMode::WEIGHT ? sim_single_target : USER_SINGLE_ESPRESSO_TIME_S,
        current_mode == GrindMode::WEIGHT ? USER_DOUBLE_ESPRESSO_WEIGHT_G : USER_DOUBLE_ESPRESSO_TIME_S,
        current_mode == GrindMode::WEIGHT ? USER_CUSTOM_PROFILE_WEIGHT_G : USER_CUSTOM_PROFILE_TIME_S
    };
    ready_screen.update_profile_values(values, current_mode);
    arc_screen.set_time_mode(current_mode == GrindMode::TIME);
    chart_screen.set_time_mode(current_mode == GrindMode::TIME);
    disc_screen.set_time_mode(current_mode == GrindMode::TIME);
}

void cycle_single_target() {
    if (std::abs(sim_single_target - 18.0f) < 0.05f) {
        sim_single_target = 17.4f;
    } else if (std::abs(sim_single_target - 17.4f) < 0.05f) {
        sim_single_target = 17.0f;
    } else if (std::abs(sim_single_target - 17.0f) < 0.05f) {
        sim_single_target = 18.5f;
    } else if (std::abs(sim_single_target - 18.5f) < 0.05f) {
        sim_single_target = 19.0f;
    } else {
        sim_single_target = 18.0f;
    }
    update_ready_screen_profiles();
    update_menu_labels();
}

void update_menu_labels() {
    if (anim_btn_label) {
        const char* anim_name = (active_layout == SimGrindLayout::DISC_RING) ? "DISC Ring (Halo)" :
                                (active_layout == SimGrindLayout::DISC_SOLID) ? "DISC Solid (Green)" :
                                (active_layout == SimGrindLayout::CHART) ? "CHART (Graph)" : "ARC (Gauge)";
        char buf[64];
        snprintf(buf, sizeof(buf), "Layout: %s", anim_name);
        lv_label_set_text(anim_btn_label, buf);
    }
    if (mode_btn_label) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Mode: %s", (current_mode == GrindMode::TIME) ? "TIME (s)" : "WEIGHT (g)");
        lv_label_set_text(mode_btn_label, buf);
    }
    if (icons_btn_label) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Grindmode: %s", ready_screen.is_show_mode_icons() ? "Icons" : "Text Labels");
        lv_label_set_text(icons_btn_label, buf);
    }
    if (disc_color_btn) {
        if (active_layout == SimGrindLayout::DISC_RING || active_layout == SimGrindLayout::DISC_SOLID) {
            lv_obj_clear_flag(disc_color_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(disc_color_btn, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_bg_color(disc_color_btn, lv_color_hex(disc_screen.get_color_preset_def().color_hex), 0);
    }
    if (disc_color_btn_label) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Disc Color: %s", disc_screen.get_color_preset_def().name);
        lv_label_set_text(disc_color_btn_label, buf);
    }
    if (target_btn_label) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Single Target: %.1fg", sim_single_target);
        lv_label_set_text(target_btn_label, buf);
    }
}

void toggle_mode_icons() {
    const bool next = !ready_screen.is_show_mode_icons();
    ready_screen.set_show_mode_icons(next);
    update_menu_labels();
    set_status(next ? "GRINDMODE: ICONS" : "GRINDMODE: TEXT");
}

void cycle_disc_color() {
    int cur = disc_screen.get_color_preset();
    int next = (cur + 1) % NUM_DISC_COLOR_PRESETS;
    disc_screen.set_color_preset(next);
    update_menu_labels();
    char buf[64];
    snprintf(buf, sizeof(buf), "DISC COLOR: %s", disc_screen.get_color_preset_def().name);
    set_status(buf);
}

void open_menu() {
    sim_log("open_menu() called\n");
    menu_open = true;
    ready_screen.hide();
    arc_screen.hide();
    disc_screen.hide();
    chart_screen.hide();
    if (grind_button) {
        lv_obj_add_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
    }
    if (pulse_button) {
        lv_obj_add_flag(pulse_button, LV_OBJ_FLAG_HIDDEN);
    }
    if (sim_menu_screen) {
        lv_obj_clear_flag(sim_menu_screen, LV_OBJ_FLAG_HIDDEN);
    }
    update_menu_labels();
    set_status("SETTINGS MENU");
}

void close_menu() {
    sim_log("close_menu() called\n");
    menu_open = false;
    if (sim_menu_screen) {
        lv_obj_add_flag(sim_menu_screen, LV_OBJ_FLAG_HIDDEN);
    }
    show_ready();
}

void update_grinder_controls_state() {
    if (menu_open) {
        if (grind_button) lv_obj_add_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
        if (pulse_button) lv_obj_add_flag(pulse_button, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!ready_screen.is_visible()) {
        return;
    }
    lv_obj_t* tv = ready_screen.get_tabview();
    int act_tab = tv ? lv_tabview_get_tab_act(tv) : 0;
    last_seen_tab = act_tab;

    if (act_tab == ready_screen.menu_tab_index()) {
        set_grind_button(LV_SYMBOL_SETTINGS, THEME_COLOR_NEUTRAL);
        lv_obj_clear_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
        set_status("SETTINGS TAB");
    } else if (act_tab == ready_screen.wifi_tab_index()) {
        set_grind_button(LV_SYMBOL_WIFI, THEME_COLOR_NEUTRAL);
        lv_obj_clear_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
        set_status("WIFI TAB");
    } else {
        uint32_t color = THEME_COLOR_PRIMARY;
        if (act_tab == ready_screen.manual_tab_index()) {
            color = THEME_COLOR_SUCCESS;
        } else if (current_mode == GrindMode::TIME) {
            color = THEME_COLOR_ACCENT;
        }
        set_grind_button(LV_SYMBOL_PLAY, color);
        lv_obj_clear_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
        set_status("READY");
    }
}

void toggle_grind_mode() {
    current_mode = (current_mode == GrindMode::WEIGHT) ? GrindMode::TIME : GrindMode::WEIGHT;
    update_ready_screen_profiles();
    update_grinder_controls_state();
    update_menu_labels();
    set_status(current_mode == GrindMode::TIME ? "TIME MODE" : "WEIGHT MODE");
}

void show_ready() {
    sim_log("show_ready() called\n");
    grinding = false;
    settling = false;
    manual_grinding = false;
    menu_open = false;
    if (sim_menu_screen) {
        lv_obj_add_flag(sim_menu_screen, LV_OBJ_FLAG_HIDDEN);
    }
    arc_screen.hide();
    disc_screen.hide();
    chart_screen.hide();
    disc_screen.set_completed(false);
    arc_screen.set_manual_mode(false);
    disc_screen.set_manual_mode(false);
    ready_screen.show();
    ready_screen.update_manual_scale(0.0f, true, false);
    update_grinder_controls_state();
}

void show_active_grind_screen() {
    sim_log("show_active_grind_screen layout=%d\n", static_cast<int>(active_layout));
    ready_screen.hide();
    arc_screen.hide();
    disc_screen.hide();
    chart_screen.hide();
    if (sim_menu_screen) {
        lv_obj_add_flag(sim_menu_screen, LV_OBJ_FLAG_HIDDEN);
    }
    if (active_layout == SimGrindLayout::DISC_RING || active_layout == SimGrindLayout::DISC_SOLID) {
        disc_screen.set_finish_style(active_layout == SimGrindLayout::DISC_SOLID
                                         ? DiscFinishStyle::SOLID_GREEN
                                         : DiscFinishStyle::ACCENT_RING);
        disc_screen.show();
        if (!grinding && !settling && !ready_screen.is_visible()) {
            if (grind_button) lv_obj_add_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (active_layout == SimGrindLayout::CHART) {
        chart_screen.show();
        if (!grinding && !settling && !ready_screen.is_visible()) {
            if (grind_button) lv_obj_clear_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        arc_screen.show();
        if (!grinding && !settling && !ready_screen.is_visible()) {
            if (grind_button) lv_obj_clear_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
    sim_log("show_active_grind_screen finished\n");
}

void reset_grind_views() {
    sim_log("reset_grind_views: name=%s target=%.2f\n", active_profile_name, active_target);
    disc_screen.set_completed(false);
    arc_screen.update_profile_name(active_profile_name);
    chart_screen.update_profile_name(active_profile_name);
    disc_screen.update_profile_name(active_profile_name);

    if (manual_grinding) {
        arc_screen.update_target_weight_text("");
        chart_screen.update_target_weight_text("Single Dose");
        disc_screen.update_target_weight_text("");
    } else if (current_mode == GrindMode::TIME) {
        arc_screen.update_target_time(active_target);
        chart_screen.update_target_time(active_target);
        disc_screen.update_target_time(active_target);
    } else {
        arc_screen.update_target_weight(active_target);
        chart_screen.update_target_weight(active_target);
        disc_screen.update_target_weight(active_target);
    }

    arc_screen.update_current_weight(0.0f);
    chart_screen.update_current_weight(0.0f);
    disc_screen.update_current_weight(0.0f);
    arc_screen.update_progress(0);
    disc_screen.update_progress(0);
    chart_screen.reset_chart_data();
    sim_log("reset_grind_views finished\n");
}

void start_grind() {
    const int act_tab = ready_screen.get_tabview() ? lv_tabview_get_tab_act(ready_screen.get_tabview()) : 0;
    if (act_tab == ready_screen.menu_tab_index()) {
        open_menu();
        return;
    }
    if (act_tab == ready_screen.wifi_tab_index()) {
        return;
    }

    weight_g = 0.0f;
    flow_gps = 0.0f;
    grinding = true;
    settling = false;

    manual_grinding = (act_tab == ReadyScreen::MANUAL_TAB_INDEX);

    if (manual_grinding) {
        active_profile_name = "MANUAL";
        active_target = 0.0f;
    } else if (act_tab == 2) {
        active_profile_name = "DOUBLE";
        active_target = (current_mode == GrindMode::WEIGHT) ? USER_DOUBLE_ESPRESSO_WEIGHT_G : USER_DOUBLE_ESPRESSO_TIME_S;
    } else if (act_tab == 3) {
        active_profile_name = "CUSTOM";
        active_target = (current_mode == GrindMode::WEIGHT) ? USER_CUSTOM_PROFILE_WEIGHT_G : USER_CUSTOM_PROFILE_TIME_S;
    } else {
        active_profile_name = "SINGLE";
        active_target = (current_mode == GrindMode::WEIGHT) ? sim_single_target : USER_SINGLE_ESPRESSO_TIME_S;
    }

    sim_log("start_grind: act_tab=%d name=%s target=%.2f mode=%d layout=%d\n",
            act_tab, active_profile_name, active_target, static_cast<int>(current_mode), static_cast<int>(active_layout));

    grind_started_ms = now_ms();
    last_update_ms = grind_started_ms;
    arc_screen.set_manual_mode(manual_grinding);
    disc_screen.set_manual_mode(manual_grinding);
    reset_grind_views();
    show_active_grind_screen();
    set_grind_button(LV_SYMBOL_STOP, THEME_COLOR_PRIMARY);
    set_status("GRINDING");
    sim_log("start_grind finished\n");
}

void toggle_layout() {
    if (active_layout == SimGrindLayout::ARC) {
        active_layout = SimGrindLayout::DISC_RING;
    } else if (active_layout == SimGrindLayout::DISC_RING) {
        active_layout = SimGrindLayout::DISC_SOLID;
    } else if (active_layout == SimGrindLayout::DISC_SOLID) {
        active_layout = SimGrindLayout::CHART;
    } else {
        active_layout = SimGrindLayout::ARC;
    }
    if (!ready_screen.is_visible() && !menu_open) {
        show_active_grind_screen();
    }
    update_menu_labels();
    set_status(active_layout == SimGrindLayout::DISC_RING ? "DISC RING VIEW" :
               active_layout == SimGrindLayout::DISC_SOLID ? "DISC SOLID VIEW" :
               active_layout == SimGrindLayout::CHART ? "CHART VIEW" : "ARC VIEW");
}

void tare() {
    weight_g = 0.0f;
    arc_screen.update_tare_display();
    disc_screen.update_tare_display();
    chart_screen.update_tare_display();
    if (ready_screen.is_visible()) {
        ready_screen.update_manual_scale(0.0f, true, false);
    }
    set_status("TARE");
}

void grind_button_event(lv_event_t* e) {
    sim_log("grind_button_event: e=%p ready_vis=%d menu_open=%d grinding=%d settling=%d\n",
            e, ready_screen.is_visible() ? 1 : 0, menu_open ? 1 : 0, grinding ? 1 : 0, settling ? 1 : 0);
    if (menu_open) {
        return;
    }
    if (ready_screen.is_visible()) {
        const int act_tab = ready_screen.get_tabview() ? lv_tabview_get_tab_act(ready_screen.get_tabview()) : 0;
        if (act_tab == ready_screen.menu_tab_index()) {
            open_menu();
            return;
        }
        if (act_tab == ready_screen.wifi_tab_index()) {
            return;
        }
        start_grind();
    } else if (grinding || settling) {
        grinding = false;
        settling = false;
        manual_grinding = false;
        if (demo_mode) {
            demo_mode = false;
            open_menu();
        } else {
            set_grind_button(LV_SYMBOL_OK, THEME_COLOR_SUCCESS);
            set_status("STOPPED");
        }
    } else {
        if (demo_mode) {
            demo_mode = false;
            open_menu();
        } else {
            show_ready();
        }
    }
}

void switch_tab(int delta) {
    if (menu_open) {
        close_menu();
        return;
    }
    if (!ready_screen.is_visible()) return;
    lv_obj_t* tv = ready_screen.get_tabview();
    if (!tv) return;
    int cur = lv_tabview_get_tab_act(tv);
    int cnt = ready_screen.tab_count();
    int next_tab = cur + delta;
    if (next_tab >= 0 && next_tab < cnt) {
        lv_tabview_set_act(tv, next_tab, LV_ANIM_ON);
        update_grinder_controls_state();
    }
}

void set_tab(int tab_index) {
    if (menu_open) {
        close_menu();
    }
    if (!ready_screen.is_visible()) return;
    lv_obj_t* tv = ready_screen.get_tabview();
    if (!tv) return;
    int cnt = ready_screen.tab_count();
    if (tab_index >= 0 && tab_index < cnt) {
        lv_tabview_set_act(tv, tab_index, LV_ANIM_ON);
        update_grinder_controls_state();
    }
}

void create_sim_menu_screen() {
    sim_menu_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(sim_menu_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_align(sim_menu_screen, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(sim_menu_screen, lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(sim_menu_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sim_menu_screen, 0, 0);
    lv_obj_set_style_pad_all(sim_menu_screen, 12, 0);
    lv_obj_set_style_pad_gap(sim_menu_screen, 8, 0);
    lv_obj_set_layout(sim_menu_screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sim_menu_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sim_menu_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Title
    lv_obj_t* title_label = lv_label_create(sim_menu_screen);
    lv_label_set_text(title_label, "SETTINGS");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(THEME_COLOR_SECONDARY), 0);
    lv_obj_set_style_margin_bottom(title_label, 2, 0);

    // Subtitle
    lv_obj_t* sub_label = lv_label_create(sim_menu_screen);
    lv_label_set_text(sub_label, "Simulator Options");
    lv_obj_set_style_text_font(sub_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_margin_bottom(sub_label, 6, 0);

    // 1. Layout Button
    lv_obj_t* anim_btn = lv_button_create(sim_menu_screen);
    lv_obj_set_size(anim_btn, LV_PCT(100), 38);
    lv_obj_set_style_bg_color(anim_btn, lv_color_hex(0x282828), 0);
    lv_obj_set_style_radius(anim_btn, 8, 0);
    lv_obj_set_style_border_width(anim_btn, 1, 0);
    lv_obj_set_style_border_color(anim_btn, lv_color_hex(0x404040), 0);
    lv_obj_add_event_cb(anim_btn, [](lv_event_t*) {
        toggle_layout();
    }, LV_EVENT_CLICKED, nullptr);

    anim_btn_label = lv_label_create(anim_btn);
    lv_obj_set_style_text_font(anim_btn_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(anim_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(anim_btn_label);

    // 2. Demo Animation Button
    lv_obj_t* demo_btn = lv_button_create(sim_menu_screen);
    lv_obj_set_size(demo_btn, LV_PCT(100), 38);
    lv_obj_set_style_bg_color(demo_btn, lv_color_hex(0x1B6B48), 0);
    lv_obj_set_style_radius(demo_btn, 8, 0);
    lv_obj_set_style_border_width(demo_btn, 1, 0);
    lv_obj_set_style_border_color(demo_btn, lv_color_hex(0x28A745), 0);
    lv_obj_add_event_cb(demo_btn, [](lv_event_t*) {
        demo_mode = true;
        close_menu();
        start_grind();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* demo_btn_label = lv_label_create(demo_btn);
    lv_label_set_text(demo_btn_label, LV_SYMBOL_PLAY "  Demo Animation");
    lv_obj_set_style_text_font(demo_btn_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(demo_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(demo_btn_label);

    // 3. Mode Button (Weight / Time)
    lv_obj_t* mode_btn = lv_button_create(sim_menu_screen);
    lv_obj_set_size(mode_btn, LV_PCT(100), 38);
    lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0x282828), 0);
    lv_obj_set_style_radius(mode_btn, 8, 0);
    lv_obj_set_style_border_width(mode_btn, 1, 0);
    lv_obj_set_style_border_color(mode_btn, lv_color_hex(0x404040), 0);
    lv_obj_add_event_cb(mode_btn, [](lv_event_t*) {
        toggle_grind_mode();
    }, LV_EVENT_CLICKED, nullptr);

    mode_btn_label = lv_label_create(mode_btn);
    lv_obj_set_style_text_font(mode_btn_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(mode_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(mode_btn_label);

    // 4. Mode Icons / Text Button
    lv_obj_t* icons_btn = lv_button_create(sim_menu_screen);
    lv_obj_set_size(icons_btn, LV_PCT(100), 38);
    lv_obj_set_style_bg_color(icons_btn, lv_color_hex(0x282828), 0);
    lv_obj_set_style_radius(icons_btn, 8, 0);
    lv_obj_set_style_border_width(icons_btn, 1, 0);
    lv_obj_set_style_border_color(icons_btn, lv_color_hex(0x404040), 0);
    lv_obj_add_event_cb(icons_btn, [](lv_event_t*) {
        toggle_mode_icons();
    }, LV_EVENT_CLICKED, nullptr);

    icons_btn_label = lv_label_create(icons_btn);
    lv_obj_set_style_text_font(icons_btn_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(icons_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(icons_btn_label);

    // 5. Disc Color Button
    disc_color_btn = lv_button_create(sim_menu_screen);
    lv_obj_set_size(disc_color_btn, LV_PCT(100), 38);
    lv_obj_set_style_bg_color(disc_color_btn, lv_color_hex(0x282828), 0);
    lv_obj_set_style_radius(disc_color_btn, 8, 0);
    lv_obj_set_style_border_width(disc_color_btn, 1, 0);
    lv_obj_set_style_border_color(disc_color_btn, lv_color_hex(0x404040), 0);
    lv_obj_add_event_cb(disc_color_btn, [](lv_event_t*) {
        cycle_disc_color();
    }, LV_EVENT_CLICKED, nullptr);

    disc_color_btn_label = lv_label_create(disc_color_btn);
    lv_obj_set_style_text_font(disc_color_btn_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(disc_color_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(disc_color_btn_label);

    // 6. Target Adjustment Button (Single Dose)
    lv_obj_t* target_btn = lv_button_create(sim_menu_screen);
    lv_obj_set_size(target_btn, LV_PCT(100), 38);
    lv_obj_set_style_bg_color(target_btn, lv_color_hex(0x282828), 0);
    lv_obj_set_style_radius(target_btn, 8, 0);
    lv_obj_set_style_border_width(target_btn, 1, 0);
    lv_obj_set_style_border_color(target_btn, lv_color_hex(0x404040), 0);
    lv_obj_add_event_cb(target_btn, [](lv_event_t*) {
        cycle_single_target();
    }, LV_EVENT_CLICKED, nullptr);

    target_btn_label = lv_label_create(target_btn);
    lv_obj_set_style_text_font(target_btn_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(target_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(target_btn_label);

    // 7. Tare Button
    lv_obj_t* tare_btn = lv_button_create(sim_menu_screen);
    lv_obj_set_size(tare_btn, LV_PCT(100), 38);
    lv_obj_set_style_bg_color(tare_btn, lv_color_hex(0x282828), 0);
    lv_obj_set_style_radius(tare_btn, 8, 0);
    lv_obj_set_style_border_width(tare_btn, 1, 0);
    lv_obj_set_style_border_color(tare_btn, lv_color_hex(0x404040), 0);
    lv_obj_add_event_cb(tare_btn, [](lv_event_t*) {
        tare();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* tare_btn_label = lv_label_create(tare_btn);
    lv_label_set_text(tare_btn_label, "Tare Scale");
    lv_obj_set_style_text_font(tare_btn_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(tare_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(tare_btn_label);

    // 8. Instructions Box
    lv_obj_t* info_box = lv_obj_create(sim_menu_screen);
    lv_obj_set_size(info_box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(info_box, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_radius(info_box, 8, 0);
    lv_obj_set_style_border_width(info_box, 0, 0);
    lv_obj_set_style_pad_all(info_box, 8, 0);
    lv_obj_clear_flag(info_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* info_text = lv_label_create(info_box);
    lv_label_set_text(info_text,
        "Shortcuts:\n"
        "- Drag: Swipe Tabs\n"
        "- 1-6: Direct Tab Select\n"
        "- Space: Grind / Stop\n"
        "- V: Animation View\n"
        "- M: Weight / Time Mode\n"
        "- I: Mode Icons / Text\n"
        "- C: Disc Color\n"
        "- T: Tare Scale\n"
        "- Esc: Back to Ready");
    lv_obj_set_style_text_font(info_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info_text, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);

    // 8. Back Button
    lv_obj_t* back_btn = lv_button_create(sim_menu_screen);
    lv_obj_set_size(back_btn, LV_PCT(100), 42);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(THEME_COLOR_PRIMARY), 0);
    lv_obj_set_style_radius(back_btn, 10, 0);
    lv_obj_add_event_cb(back_btn, [](lv_event_t*) {
        close_menu();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* back_btn_label = lv_label_create(back_btn);
    lv_label_set_text(back_btn_label, LV_SYMBOL_LEFT "  BACK TO READY");
    lv_obj_set_style_text_font(back_btn_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(back_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_btn_label);

    update_menu_labels();
    lv_obj_add_flag(sim_menu_screen, LV_OBJ_FLAG_HIDDEN);
}

void create_grinder_controls() {
    grind_button = lv_button_create(lv_screen_active());
    lv_obj_set_size(grind_button, 100, 100);
    lv_obj_align(grind_button, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(grind_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(grind_button, lv_color_hex(THEME_COLOR_PRIMARY), 0);
    lv_obj_set_style_border_width(grind_button, 0, 0);
    lv_obj_set_style_shadow_width(grind_button, 0, 0);
    lv_obj_add_event_cb(grind_button, grind_button_event, LV_EVENT_CLICKED, nullptr);

    grind_icon = lv_image_create(grind_button);
    lv_image_set_src(grind_icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(grind_icon, &lv_font_montserrat_24, 0);
    lv_obj_center(grind_icon);

    pulse_button = lv_button_create(lv_screen_active());
    lv_obj_set_size(pulse_button, 100, 100);
    lv_obj_align(pulse_button, LV_ALIGN_BOTTOM_MID, 60, -10);
    lv_obj_set_style_radius(pulse_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pulse_button, lv_color_hex(THEME_COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(pulse_button, 0, 0);
    lv_obj_set_style_shadow_width(pulse_button, 0, 0);
    lv_obj_add_flag(pulse_button, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* pulse_icon = lv_image_create(pulse_button);
    lv_image_set_src(pulse_icon, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(pulse_icon, &lv_font_montserrat_32, 0);
    lv_obj_center(pulse_icon);
}

void update_mock_grind() {
    if (!grinding && !settling) {
        return;
    }

    const uint32_t current_ms = now_ms();
    const float dt_s = std::min(0.1f, static_cast<float>(current_ms - last_update_ms) / 1000.0f);
    last_update_ms = current_ms;
    const float elapsed_s = static_cast<float>(current_ms - grind_started_ms) / 1000.0f;

    if (grinding) {
        flow_gps = std::min(1.75f, 0.25f + elapsed_s * 0.55f);
        weight_g += flow_gps * dt_s;
        if (manual_grinding) {
            char elapsed_text[32];
            std::snprintf(elapsed_text, sizeof(elapsed_text), "Elapsed: %.1fs", elapsed_s);
            arc_screen.update_target_weight_text(elapsed_text);
            disc_screen.update_target_weight_text(elapsed_text);
            chart_screen.update_target_weight_text(elapsed_text);
        } else if (current_mode == GrindMode::TIME) {
            if (elapsed_s >= active_target) {
                grinding = false;
                settling = false;
                flow_gps = 0.0f;
                disc_screen.set_completed(true);
                if (active_layout == SimGrindLayout::DISC_RING || active_layout == SimGrindLayout::DISC_SOLID) {
                    if (grind_button) lv_obj_add_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
                } else {
                    set_grind_button(LV_SYMBOL_OK, THEME_COLOR_SUCCESS);
                }
                set_status("COMPLETE");
            }
        } else if (weight_g >= active_target - 0.45f) {
            grinding = false;
            settling = true;
            set_status("SETTLING");
        }
    } else if (settling) {
        flow_gps *= std::pow(0.12f, dt_s);
        weight_g += flow_gps * dt_s;
        if (flow_gps < 0.015f || weight_g >= active_target) {
            settling = false;
            flow_gps = 0.0f;
            weight_g = active_target;
            disc_screen.set_completed(true);
            if (active_layout == SimGrindLayout::DISC_RING || active_layout == SimGrindLayout::DISC_SOLID) {
                if (grind_button) lv_obj_add_flag(grind_button, LV_OBJ_FLAG_HIDDEN);
            } else {
                set_grind_button(LV_SYMBOL_OK, THEME_COLOR_SUCCESS);
            }
            set_status("COMPLETE");
        }
    }

    int progress = 0;
    if (manual_grinding) {
        progress = 0;
    } else if (current_mode == GrindMode::TIME) {
        progress = std::clamp(static_cast<int>((elapsed_s / std::max(0.1f, active_target)) * 100.0f), 0, 100);
    } else {
        progress = std::clamp(static_cast<int>((weight_g / std::max(0.1f, active_target)) * 100.0f), 0, 100);
    }

    arc_screen.update_current_weight(weight_g);
    disc_screen.update_current_weight(weight_g);
    chart_screen.update_current_weight(weight_g);
    arc_screen.update_progress(progress);
    disc_screen.update_progress(progress);
    chart_screen.add_chart_data_point(weight_g, flow_gps, current_ms);
}

bool has_arg(int argc, char** argv, const char* expected) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], expected) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    _set_abort_behavior(0, _WRITE_ABORT_MSG);
    signal(SIGABRT, [](int) {
        sim_log("FATAL: SIGABRT (assertion failed or abort)\n");
        MessageBoxA(NULL, "SIGABRT: An assertion failed or abort() was called.", "Simulator Error", MB_OK | MB_ICONERROR);
        ExitProcess(99);
    });

    _set_invalid_parameter_handler([](const wchar_t* expr, const wchar_t* func, const wchar_t* file, unsigned int line, uintptr_t) {
        char buf[512];
        snprintf(buf, sizeof(buf), "Invalid parameter in %ls (%ls:%u): %ls\n", func ? func : L"", file ? file : L"", line, expr ? expr : L"");
        sim_log("FATAL: %s", buf);
        MessageBoxA(NULL, buf, "Simulator Error", MB_OK | MB_ICONERROR);
        ExitProcess(98);
    });

    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* pException) -> LONG {
        char buf[512];
        snprintf(buf, sizeof(buf), "CRASH! ExceptionCode: 0x%08lX at Address: %p\n",
            pException->ExceptionRecord->ExceptionCode,
            pException->ExceptionRecord->ExceptionAddress);
        sim_log("FATAL: %s", buf);
        MessageBoxA(NULL, buf, "Simulator Crash", MB_OK | MB_ICONERROR);
        return EXCEPTION_CONTINUE_SEARCH;
    });

    sim_log("Simulator starting...\n");

    const bool smoke_test = has_arg(argc, argv, "--smoke");
    const bool benchmark = has_arg(argc, argv, "--benchmark");
    const bool swipe_benchmark = has_arg(argc, argv, "--swipe-benchmark");
    const bool manual_smoke = has_arg(argc, argv, "--manual-smoke");
    const bool no_icons = has_arg(argc, argv, "--no-icons") ||
                          has_arg(argc, argv, "--text-labels") ||
                          has_arg(argc, argv, "--text-mode");
    const bool dark_gray = has_arg(argc, argv, "--dark-gray") || has_arg(argc, argv, "--charcoal");
    const bool espresso_color = has_arg(argc, argv, "--espresso");
    const bool midnight_color = has_arg(argc, argv, "--midnight");
    const bool burgundy_color = has_arg(argc, argv, "--burgundy");

    if (smoke_test) {
        std::puts("[smoke] starting simulator");
        std::fflush(stdout);
    }

    lv_init();
    lv_tick_set_cb(now_ms);

    lv_display_t* display = lv_windows_create_display(
        L"Smart Grind-by-Weight Simulator",
        kDisplayWidth,
        kDisplayHeight,
        140,
        true,
        true);
    if (!display) {
        std::fprintf(stderr, "Could not create the simulator display.\n");
        return 1;
    }

    lv_indev_t* pointer_indev = lv_windows_acquire_pointer_indev(display);
    if (!pointer_indev) {
        std::fprintf(stderr, "Could not acquire pointer input device.\n");
    }

    simulator_window = lv_windows_get_display_window_handle(display);
    if (smoke_test || benchmark || swipe_benchmark || manual_smoke) {
        ShowWindow(simulator_window, SW_HIDE);
    }
    lv_display_add_event_cb(display, display_metrics_event, LV_EVENT_ALL, nullptr);

    // The Windows driver creates its framebuffer on the first LVGL timer pass.
    // Let that happen before sizing and styling the production screen objects.
    Sleep(LV_DEF_REFR_PERIOD + 1);
    lv_timer_handler();

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(THEME_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    ready_screen.create();
    if (no_icons) {
        ready_screen.set_show_mode_icons(false);
    }
    ready_screen.set_manual_tare_handler([](lv_event_t*) { tare(); });
    arc_screen.create();
    disc_screen.create();
    if (dark_gray) {
        disc_screen.set_color_preset(1);
    } else if (espresso_color) {
        disc_screen.set_color_preset(2);
    } else if (midnight_color) {
        disc_screen.set_color_preset(3);
    } else if (burgundy_color) {
        disc_screen.set_color_preset(4);
    }
    chart_screen.create();
    auto screen_click_cb = [](lv_event_t*) {
        if (demo_mode && !grinding && !settling) {
            demo_mode = false;
            open_menu();
            return;
        }
        if (!grinding && !settling && !ready_screen.is_visible() &&
            (active_layout == SimGrindLayout::DISC_RING || active_layout == SimGrindLayout::DISC_SOLID)) {
            show_ready();
            return;
        }
        toggle_layout();
    };
    if (arc_screen.get_screen()) lv_obj_add_event_cb(arc_screen.get_screen(), screen_click_cb, LV_EVENT_CLICKED, nullptr);
    if (disc_screen.get_screen()) lv_obj_add_event_cb(disc_screen.get_screen(), screen_click_cb, LV_EVENT_CLICKED, nullptr);
    if (chart_screen.get_screen()) lv_obj_add_event_cb(chart_screen.get_screen(), screen_click_cb, LV_EVENT_CLICKED, nullptr);
    create_sim_menu_screen();
    if (ready_screen.get_menu_tab()) {
        lv_obj_add_flag(ready_screen.get_menu_tab(), LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(ready_screen.get_menu_tab(), [](lv_event_t*) {
            open_menu();
        }, LV_EVENT_CLICKED, nullptr);
        for (uint32_t i = 0; i < lv_obj_get_child_cnt(ready_screen.get_menu_tab()); ++i) {
            lv_obj_t* child = lv_obj_get_child(ready_screen.get_menu_tab(), i);
            lv_obj_add_flag(child, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(child, [](lv_event_t*) {
                open_menu();
            }, LV_EVENT_CLICKED, nullptr);
        }
    }
    if (ready_screen.get_tabview()) {
        lv_obj_add_event_cb(ready_screen.get_tabview(), [](lv_event_t*) {
            update_grinder_controls_state();
        }, LV_EVENT_VALUE_CHANGED, nullptr);
    }
    create_grinder_controls();
    update_ready_screen_profiles();
    show_ready();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display);

    if (g_title_dirty && simulator_window) {
        g_title_dirty = false;
        SetWindowTextW(simulator_window, g_pending_title);
    }

    if (smoke_test) {
        std::puts("[smoke] UI created");
        std::fflush(stdout);
    }

    HWND window = simulator_window;
    const uint32_t smoke_started_ms = now_ms();
    bool smoke_scenario_started = false;
    bool benchmark_switched_layout = false;
    uint64_t benchmark_arc_pixels = 0;
    bool swipe_started = false;
    uint32_t swipe_started_ms = 0;
    bool view_key_down = false;
    bool tare_key_down = false;
    bool icons_key_down = false;
    bool color_key_down = false;
    bool space_key_down = false;
    bool esc_key_down = false;
    bool left_key_down = false;
    bool right_key_down = false;
    bool mode_key_down = false;
    bool num_key_down[6] = {false};
    bool manual_stop_sent = false;

    while (IsWindow(window)) {
        if (ready_screen.is_visible()) {
            int cur_tab = ready_screen.get_tabview() ? lv_tabview_get_tab_act(ready_screen.get_tabview()) : 0;
            if (cur_tab != last_seen_tab) {
                update_grinder_controls_state();
            }
        }

        if (swipe_benchmark && !swipe_started && now_ms() - smoke_started_ms > 100) {
            reset_render_metrics();
            swipe_started_ms = now_ms();
            lv_tabview_set_act(ready_screen.get_tabview(), 1, LV_ANIM_ON);
            swipe_started = true;
        }

        if ((smoke_test || benchmark) && !smoke_scenario_started && now_ms() - smoke_started_ms > 100) {
            lv_tabview_set_act(ready_screen.get_tabview(), ReadyScreen::PROFILE_TAB_START_INDEX, LV_ANIM_OFF);
            start_grind();
            smoke_scenario_started = true;
            reset_render_metrics();
        }

        if (manual_smoke && !smoke_scenario_started && now_ms() - smoke_started_ms > 100) {
            if (lv_tabview_get_tab_act(ready_screen.get_tabview()) != ReadyScreen::MANUAL_TAB_INDEX) {
                std::fprintf(stderr, "Manual page is not the first ready tab.\n");
                ExitProcess(5);
            }
            start_grind();
            smoke_scenario_started = true;
        }

        update_mock_grind();

        DWORD window_pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &window_pid);
        const bool has_focus = (window_pid == GetCurrentProcessId());

        if (has_focus) {
            const bool esc_pressed = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
            if (esc_pressed && !esc_key_down) {
                if (menu_open) {
                    close_menu();
                }
            }
            esc_key_down = esc_pressed;

            const bool view_pressed = (GetAsyncKeyState('V') & 0x8000) != 0;
            if (view_pressed && !view_key_down) {
                toggle_layout();
            }
            view_key_down = view_pressed;

            const bool tare_pressed = (GetAsyncKeyState('T') & 0x8000) != 0;
            if (tare_pressed && !tare_key_down) {
                tare();
            }
            tare_key_down = tare_pressed;

            const bool icons_pressed = (GetAsyncKeyState('I') & 0x8000) != 0;
            if (icons_pressed && !icons_key_down) {
                toggle_mode_icons();
            }
            icons_key_down = icons_pressed;

            const bool color_pressed = (GetAsyncKeyState('C') & 0x8000) != 0;
            if (color_pressed && !color_key_down) {
                cycle_disc_color();
            }
            color_key_down = color_pressed;

            const bool space_pressed = ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0) ||
                                       ((GetAsyncKeyState(VK_RETURN) & 0x8000) != 0);
            if (space_pressed && !space_key_down) {
                sim_log("Keyboard SPACE pressed\n");
                if (menu_open) {
                    close_menu();
                } else {
                    grind_button_event(nullptr);
                }
            }
            space_key_down = space_pressed;

            const bool left_pressed = ((GetAsyncKeyState(VK_LEFT) & 0x8000) != 0) ||
                                      ((GetAsyncKeyState('A') & 0x8000) != 0);
            if (left_pressed && !left_key_down) {
                switch_tab(-1);
            }
            left_key_down = left_pressed;

            const bool right_pressed = ((GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0) ||
                                       ((GetAsyncKeyState('D') & 0x8000) != 0);
            if (right_pressed && !right_key_down) {
                switch_tab(1);
            }
            right_key_down = right_pressed;

            const bool mode_pressed = ((GetAsyncKeyState('M') & 0x8000) != 0) ||
                                      ((GetAsyncKeyState(VK_UP) & 0x8000) != 0) ||
                                      ((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0);
            if (mode_pressed && !mode_key_down) {
                toggle_grind_mode();
            }
            mode_key_down = mode_pressed;

            for (int i = 0; i < 6; ++i) {
                const bool num_pressed = ((GetAsyncKeyState('1' + i) & 0x8000) != 0) ||
                                         ((GetAsyncKeyState(VK_NUMPAD1 + i) & 0x8000) != 0);
                if (num_pressed && !num_key_down[i]) {
                    set_tab(i);
                }
                num_key_down[i] = num_pressed;
            }
        } else {
            view_key_down = false;
            tare_key_down = false;
            icons_key_down = false;
            color_key_down = false;
            space_key_down = false;
            esc_key_down = false;
            left_key_down = false;
            right_key_down = false;
            mode_key_down = false;
            for (int i = 0; i < 6; ++i) {
                num_key_down[i] = false;
            }
        }

        const uint32_t wait_ms = std::clamp<uint32_t>(lv_timer_handler(), 1, 16);

        if (g_title_dirty && simulator_window) {
            g_title_dirty = false;
            SetWindowTextW(simulator_window, g_pending_title);
        }

        Sleep(wait_ms);

        if (swipe_benchmark && swipe_started &&
            !lv_obj_is_scrolling(lv_tabview_get_content(ready_screen.get_tabview()))) {
            const uint32_t duration_ms = now_ms() - swipe_started_ms;
            print_render_metrics("swipe", duration_ms);
            if (lv_tabview_get_tab_act(ready_screen.get_tabview()) != 1 ||
                duration_ms > kSwipeDurationBudgetMs ||
                render_metrics.flushed_pixels > kSwipePixelBudget) {
                std::fprintf(stderr, "Simulator swipe budget exceeded.\n");
                std::fflush(stderr);
                ExitProcess(4);
            }
            ExitProcess(0);
        }

        if (smoke_test && now_ms() - smoke_started_ms > 750) {
            if (!arc_screen.is_visible() || ready_screen.is_visible() || weight_g <= 0.0f) {
                std::fprintf(stderr, "Simulator scenario did not advance.\n");
                std::fflush(stderr);
                ExitProcess(2);
            }
            std::printf("[smoke] scenario advanced to %.2fg\n", weight_g);
            std::fflush(stdout);
            ExitProcess(0);
        }

        if (manual_smoke && smoke_scenario_started && !manual_stop_sent &&
            now_ms() - smoke_started_ms > 650) {
            if (!manual_grinding || !arc_screen.is_visible() || weight_g <= 0.0f) {
                std::fprintf(stderr, "Manual grind did not advance.\n");
                ExitProcess(6);
            }
            grind_button_event(nullptr);
            manual_stop_sent = true;
        }

        if (manual_smoke && manual_stop_sent && now_ms() - smoke_started_ms > 750) {
            if (grinding || settling || manual_grinding) {
                std::fprintf(stderr, "Manual grind did not stop.\n");
                ExitProcess(7);
            }
            std::printf("[manual-smoke] start/stop completed at %.2fg\n", weight_g);
            ExitProcess(0);
        }

        if (benchmark && !benchmark_switched_layout && now_ms() - smoke_started_ms > 2600) {
            print_render_metrics("arc", 2500);
            benchmark_arc_pixels = render_metrics.flushed_pixels;
            active_layout = SimGrindLayout::CHART;
            show_active_grind_screen();
            benchmark_switched_layout = true;
            reset_render_metrics();
        }

        if (benchmark && benchmark_switched_layout && now_ms() - smoke_started_ms > 5100) {
            print_render_metrics("chart", 2500);
            if (benchmark_arc_pixels > kArcPixelBudget ||
                render_metrics.flushed_pixels > kChartPixelBudget) {
                std::fprintf(stderr, "Simulator render budget exceeded.\n");
                std::fflush(stderr);
                ExitProcess(3);
            }
            ExitProcess(0);
        }
    }

    sim_log("Simulator loop ended normally\n");
    lv_deinit();
    return 0;
}
