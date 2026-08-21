#include "controllers/net_weight_guard.h"

#include <cassert>

int main() {
    NetWeightRemovalGuard guard;

    // The reported -315.6 g spike must not look like removal of a 427 g
    // portafilter. Its removal threshold is -384.3 g.
    guard.reset(427.0f);
    assert(!guard.update(-315.6f));

    // One threshold-crossing sample is not enough to stop a grind.
    assert(!guard.update(-390.0f));
    assert(!guard.update(2.0f));

    // Leaving an active grind phase also clears a partly confirmed removal.
    assert(!guard.update(-390.0f));
    guard.cancel_pending();
    assert(!guard.update(-391.0f));

    // A sustained reading close to the full pre-tare weight is a removal.
    guard.reset(427.0f);
    assert(!guard.update(-390.0f));
    assert(!guard.update(-391.0f));
    assert(guard.update(-392.0f));

    // With no meaningful vessel weight, the removal guard stays disabled.
    guard.reset(0.0f);
    assert(!guard.has_reference());
    assert(!guard.update(-100.0f));

    return 0;
}
