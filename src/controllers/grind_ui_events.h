#pragma once
#include "grind_events.h"

struct GrindUIEventBatch {
    GrindEventData state{};
    GrindEventData progress{};
    GrindEventData background{};
    bool state_pending = false;
    bool progress_pending = false;
    bool background_pending = false;
};

// Latest display snapshots, not a history of every control-loop tick. The
// controller locks publish/take; callbacks run after that lock is released.
class GrindUIEventMailbox {
public:
    void publish(const GrindEventData& event) {
        switch (event.event) {
            case UIGrindEvent::BACKGROUND_CHANGE:
                pending_.background = event;
                pending_.background_pending = true;
                break;
            case UIGrindEvent::PROGRESS_UPDATED:
                // Never let readings from an earlier phase, or continuous
                // terminal-phase readings, overwrite the final result/error.
                if (state_known_ && pending_.state.event == UIGrindEvent::PHASE_CHANGED &&
                    event.phase == pending_.state.phase && event.mode == pending_.state.mode) {
                    pending_.progress = event;
                    pending_.progress_pending = true;
                }
                break;
            case UIGrindEvent::PHASE_CHANGED:
            case UIGrindEvent::COMPLETED:
            case UIGrindEvent::TIMEOUT:
            case UIGrindEvent::STOPPED:
                pending_.state = event;
                pending_.state_pending = true;
                pending_.progress_pending = false;
                state_known_ = true;
                break;
            default:
                // Legacy PULSE_* events have no producers. Additional pulses
                // use TIME_ADDITIONAL_PULSE and COMPLETED state snapshots.
                break;
        }
    }

    GrindUIEventBatch take() {
        const auto batch = pending_;
        pending_.state_pending = false;
        pending_.progress_pending = false;
        pending_.background_pending = false;
        return batch;
    }

private:
    GrindUIEventBatch pending_{};
    bool state_known_ = false;
};
