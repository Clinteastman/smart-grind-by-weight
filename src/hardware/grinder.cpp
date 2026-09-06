#include "grinder.h"
#include "../controllers/grind_events.h"
#include "../config/constants.h"
#include <driver/gpio.h>
#include <algorithm>
#include <iterator>
#if DEBUG_ENABLE_LOADCELL_MOCK
#include "mock_hx711_driver.h"
#endif

void Grinder::init(int pin) {
    if (initialized) return;
    motor_pin = pin;
    grinding = false;
    pulse_active = false;
    rmt_initialized = false;
    current_encoder = nullptr;
    motor_start_time = 0;

    // Initialize background indicator
    background_active = false;
    ui_event_callback = nullptr;

#if DEBUG_ENABLE_LOADCELL_MOCK
    initialized = true;
    return;
#endif
    
    // Initialize RMT for all motor control (both continuous and pulse)
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = (gpio_num_t)motor_pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, // 1MHz resolution = 1µs per tick
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    
    gpio_reset_pin(static_cast<gpio_num_t>(motor_pin));
    gpio_set_direction(static_cast<gpio_num_t>(motor_pin), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(motor_pin), 0);
    if (rmt_new_tx_channel(&tx_chan_config, &rmt_channel) != ESP_OK) return;
    rmt_copy_encoder_config_t encoder_config{};
    if (rmt_new_copy_encoder(&encoder_config, &current_encoder) != ESP_OK) {
        rmt_del_channel(rmt_channel);
        return;
    }
    if (rmt_enable(rmt_channel) != ESP_OK) {
        rmt_del_encoder(current_encoder);
        current_encoder = nullptr;
        rmt_del_channel(rmt_channel);
        return;
    }
    rmt_initialized = true;
    initialized = true;
}

void Grinder::start() {
#if DEBUG_ENABLE_LOADCELL_MOCK
    if (!initialized) return;
    MockHX711Driver::notify_grinder_start();
    pulse_active = false;
    grinding = true;
    motor_start_time = millis();
    emit_background_change(true);
    return;
#endif
    if (!initialized || !rmt_initialized) return;

    // Stop the old transaction before modifying its payload or encoder state.
    stop();
    if (!initialized) return;
    motor_start_time = millis();
    
    // Use RMT infinite loop for continuous grinding
    symbols[0] = {};
    symbols[0].duration0 = 32767;
    symbols[0].level0 = 1;
    symbols[0].duration1 = 32767;
    symbols[0].level1 = 1;
    
    rmt_transmit_config_t tx_config = {
        .loop_count = -1, // Infinite loop
    };
    
    if (rmt_transmit(rmt_channel, current_encoder, symbols, sizeof(symbols[0]), &tx_config) != ESP_OK) {
        LOG_BLE("[Grinder] Failed to start continuous transmission\n");
        stop();
        return;
    }
    grinding = true;
    emit_background_change(true);
}

void Grinder::stop() {
#if DEBUG_ENABLE_LOADCELL_MOCK
    if (!initialized) return;
    MockHX711Driver::notify_grinder_stop();
    grinding = false;
    pulse_active = false;
    emit_background_change(false);
    return;
#endif
    if (!initialized || !rmt_initialized) return;
    
    // Stop RMT transmission (works for both infinite loop and finite pulses)
    if (rmt_disable(rmt_channel) != ESP_OK ||
        rmt_encoder_reset(current_encoder) != ESP_OK ||
        rmt_enable(rmt_channel) != ESP_OK) {
        // Disconnect RMT from the output and refuse further starts until reboot.
        // Keep its storage alive: a failed cancellation may still reference it.
        gpio_reset_pin(static_cast<gpio_num_t>(motor_pin));
        gpio_set_direction(static_cast<gpio_num_t>(motor_pin), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(motor_pin), 0);
        initialized = false;
        rmt_initialized = false;
        LOG_BLE("[Grinder] RMT reset failed; motor disabled until reboot\n");
    }
    
    grinding = false;
    pulse_active = false;
    emit_background_change(false);
}

void Grinder::start_pulse_rmt(uint32_t duration_ms) {
#if DEBUG_ENABLE_LOADCELL_MOCK
    if (!initialized) return;
    MockHX711Driver::notify_pulse(duration_ms);
    pulse_active = true;
    grinding = true;
    motor_start_time = millis();
    emit_background_change(true);
    return;
#endif
    if (!initialized || !rmt_initialized) return;

    stop();
    if (!initialized) return;
    // Reserve one half-symbol for LOW. Covers every supported call, including
    // the one-second motor test, without multiplication overflow or allocation.
    constexpr uint32_t max_duration_ms = (kSymbolCount * 2 - 1) * 32767U / 1000U;
    if (duration_ms == 0 || duration_ms > max_duration_ms) {
        LOG_BLE("[Grinder] Pulse duration outside supported range\n");
        return;
    }
    std::fill(std::begin(symbols), std::end(symbols), rmt_symbol_word_t{});
    uint32_t remaining = duration_ms * 1000U;
    size_t halves = 0;
    while (remaining > 0) {
        const uint32_t ticks = std::min(remaining, 32767U);
        auto& symbol = symbols[halves / 2];
        if (halves % 2 == 0) {
            symbol.duration0 = ticks;
            symbol.level0 = 1;
        } else {
            symbol.duration1 = ticks;
            symbol.level1 = 1;
        }
        remaining -= ticks;
        ++halves;
    }
    // All HIGH halves are consecutive. Only the final half is LOW.
    if (halves % 2 == 0) symbols[halves / 2].duration0 = 1;
    else symbols[halves / 2].duration1 = 1;
    ++halves;
    rmt_transmit_config_t tx_config{}; // no repeats, end output LOW
    if (rmt_transmit(rmt_channel, current_encoder, symbols,
                     ((halves + 1) / 2) * sizeof(symbols[0]), &tx_config) != ESP_OK) {
        LOG_BLE("[Grinder] Failed to start pulse transmission\n");
        stop();
        return;
    }
    motor_start_time = millis();
    pulse_active = true;
    grinding = true;
    emit_background_change(true);
}

bool Grinder::is_pulse_complete() {
#if DEBUG_ENABLE_LOADCELL_MOCK
    if (!pulse_active) return true;
    if (!MockHX711Driver::is_pulse_active()) {
        pulse_active = false;
        grinding = false;
        emit_background_change(false);
        return true;
    }
    return false;
#endif
    if (!pulse_active) return true;
    
    // A queued transmission can still have a LOW GPIO before it starts.
    // Poll the driver non-blockingly, not the pin level.
    const esp_err_t result = rmt_tx_wait_all_done(rmt_channel, 0);
    if (result == ESP_OK) {
        pulse_active = false;
        grinding = false;
        emit_background_change(false);
        return true;
    }
    if (result != ESP_ERR_TIMEOUT) {
        LOG_BLE("[Grinder] Pulse completion check failed; stopping motor\n");
        stop();
        return true;
    }
    
    return false;
}

bool Grinder::is_motor_settled() const {
    // Return true if sufficient time has passed since motor start
    if (motor_start_time == 0) {
        return false;  // Motor has never started
    }
    return (millis() - motor_start_time) >= HW_GRINDER_SETTLING_TIME_MS;
}

void Grinder::set_ui_event_callback(const std::function<void(const GrindEventData&)>& callback) {
    ui_event_callback = callback;
}

void Grinder::emit_background_change(bool active) {
    if (background_active == active) {
        return; // No change
    }
    
    background_active = active;
    
    if (ui_event_callback) {
        // Properly initialize all required fields to prevent null pointer crashes
        GrindEventData event_data = {};
        event_data.event = UIGrindEvent::BACKGROUND_CHANGE;
        event_data.phase = GrindPhase::IDLE;  // Safe default
        event_data.current_weight = 0.0f;
        event_data.progress_percent = 0;
        event_data.phase_display_text = "BACKGROUND";  // Safe string for logging
        event_data.show_taring_text = false;
        event_data.background_active = active;
        
        ui_event_callback(event_data);
        
        LOG_BLE("[Grinder] Background change: %s\n", active ? "ACTIVE" : "INACTIVE");
    }
}
