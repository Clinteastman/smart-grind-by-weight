#pragma once

#include <atomic>
#include <cstdint>

// A reservation spans an operation, not a single function call. Keep it until
// the motor has stopped and cleanup is complete (or through an OTA reboot).
class OperationInterlock {
public:
    using Token = uint32_t;

    Token try_acquire() {
        Token available = state_.load();
        if (available & 1U) return 0;
        const Token reserved = available + 1U;
        return state_.compare_exchange_strong(available, reserved) ? reserved : 0;
    }

    bool release(Token token) {
        if (!(token & 1U)) return false;
        // Advance the generation so a delayed cleanup cannot release a newer
        // reservation. Unsigned rollover preserves the odd/even lock bit.
        return state_.compare_exchange_strong(token, token + 1U);
    }

    bool owns(Token token) const {
        return (token & 1U) && state_.load() == token;
    }

private:
    std::atomic<Token> state_{0};
};

inline OperationInterlock& operation_interlock() {
    static OperationInterlock interlock;
    return interlock;
}
