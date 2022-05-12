#pragma once

#include <logbroker/unified_agent/common/util/clock.h>

#include <util/datetime/base.h>
#include <util/system/env.h>

#include <chrono>

namespace NUnifiedAgent {
    using TSteadyDuration = std::chrono::steady_clock::duration;

    using TSteadyTimePoint = std::chrono::steady_clock::time_point;

    TSteadyTimePoint SteadyTimePointFromNanoseconds(ui64 ns);

    ui64 SteadyTimePointToNanoseconds(TSteadyTimePoint t);

    TSteadyTimePoint SteadyNow();

    TDuration ToDuration(TSteadyDuration duration);

    TSteadyDuration ToSteadyDuration(TDuration duration);

    TInstant ToApproximateInstant(TSteadyTimePoint t);

    struct TSteadyInstant {
        ui64 Epoch;
        TSteadyTimePoint TimePoint;

        inline static TSteadyInstant Now(ui64 epoch) noexcept {
            return TSteadyInstant{epoch, SteadyNow()};
        }

        inline friend bool operator<(const TSteadyInstant& a, const TSteadyInstant& b) noexcept {
            return a.Epoch < b.Epoch || (a.Epoch == b.Epoch && a.TimePoint < b.TimePoint);
        }

        inline friend bool operator==(const TSteadyInstant& a, const TSteadyInstant& b) noexcept {
            return a.Epoch == b.Epoch && a.TimePoint == b.TimePoint;
        }

        inline friend bool operator<=(const TSteadyInstant& a, const TSteadyInstant& b) noexcept {
            return a.Epoch < b.Epoch || (a.Epoch == b.Epoch && a.TimePoint <= b.TimePoint);
        }

        inline friend bool operator>(const TSteadyInstant& a, const TSteadyInstant& b) noexcept {
            return !(a <= b);
        }

        inline friend bool operator>=(const TSteadyInstant& a, const TSteadyInstant& b) noexcept {
            return !(a < b);
        }

        inline friend TSteadyInstant operator+(const TSteadyInstant& a, TDuration d) noexcept {
            return TSteadyInstant{a.Epoch, a.TimePoint + ToSteadyDuration(d)};
        }
    };

    TSteadyInstant FromInstant(const TInstant& t, ui64 epoch);
}
