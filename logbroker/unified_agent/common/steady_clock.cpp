#include "steady_clock.h"

#include <util/generic/singleton.h>
#include <util/stream/output.h>
#include <util/string/cast.h>
#include <util/string/printf.h>

namespace NUnifiedAgent {
    const bool UA_STEADY_CLOCK_USE_REGULAR_CLOCK =
        (GetEnv("UA_STEADY_CLOCK_USE_REGULAR_CLOCK", "") != "");

    namespace {
        struct TSteadyTimePointEpoch {
            TInstant Value;

            TSteadyTimePointEpoch() {
                auto now = TInstant::Now();
                auto steadyNow = SteadyNow();
                if (UA_STEADY_CLOCK_USE_REGULAR_CLOCK){
                    Value = TInstant::FromValue(0);
                } else {
                    Value = now - ToDuration(steadyNow.time_since_epoch());
                }
            }
        };

        void WriteToStream(IOutputStream& o, const NUnifiedAgent::TSteadyInstant& instant) {
            o << "["
              << instant.Epoch
              << "," << SteadyTimePointToNanoseconds(instant.TimePoint)
              << "," << ToApproximateInstant(instant.TimePoint)
              << "]";
        }

        template <typename TTargetDuration>
        ui64 CountOf(TSteadyDuration duration) {
            const auto result = std::chrono::duration_cast<TTargetDuration>(duration).count();
            return static_cast<ui64>(result);
        }
    }

    TSteadyTimePoint SteadyTimePointFromNanoseconds(ui64 ns) {
        return TSteadyTimePoint(std::chrono::nanoseconds(ns));
    }

    ui64 SteadyTimePointToNanoseconds(TSteadyTimePoint t) {
        return CountOf<std::chrono::nanoseconds>(t.time_since_epoch());
    }

    TSteadyTimePoint SteadyNow() {
        if (UA_STEADY_CLOCK_USE_REGULAR_CLOCK) {
            return SteadyTimePointFromNanoseconds(TClock::Now().NanoSeconds());
        }
        return std::chrono::steady_clock::now();
    }

    TDuration ToDuration(TSteadyDuration duration) {
        return TDuration::MicroSeconds(CountOf<std::chrono::microseconds>(duration));
    }

    TSteadyDuration ToSteadyDuration(TDuration duration) {
        return std::chrono::nanoseconds(duration.NanoSeconds());
    }

    TInstant ToApproximateInstant(TSteadyTimePoint t) {
        return Singleton<TSteadyTimePointEpoch>()->Value + ToDuration(t.time_since_epoch());
    }

    TSteadyInstant FromInstant(const TInstant& t, ui64 epoch) {
        const auto now = TClock::Now();
        const auto steadyNow = SteadyNow();
        return (t < now) ? TSteadyInstant{epoch, steadyNow - ToSteadyDuration(now - t)}
                         : TSteadyInstant{epoch, steadyNow + ToSteadyDuration(t - now)};
    }
}

template <>
void Out<NUnifiedAgent::TSteadyInstant>(IOutputStream& o, const NUnifiedAgent::TSteadyInstant& instant) {
    NUnifiedAgent::WriteToStream(o, instant);
}
