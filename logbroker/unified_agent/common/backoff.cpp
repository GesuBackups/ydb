#include "backoff.h"

namespace NUnifiedAgent {
    TExpBackoff::TExpBackoff(TDuration min, TDuration max)
        : Min_(min)
        , Max_(max)
        , Current_(Min_) {
    }

    TDuration TExpBackoff::Next() {
        auto next = Current_;
        if (next >= Max_) {
            return Max_;
        }
        Current_ += Current_;
        return next;
    }

    void TExpBackoff::Reset() {
        Current_ = Min_;
    }
}
