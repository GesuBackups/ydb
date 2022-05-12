#include "datetime.h"

#include <util/datetime/constants.h>
#include <util/string/builder.h>
#include <util/stream/format.h>

namespace NUnifiedAgent {
    namespace {
        ssize_t GetTimezoneOffsetSeconds() {
            const auto offsetDesc = TClock::Now().FormatLocalTime("%z");
            Y_VERIFY(offsetDesc.size() == 5);
            const auto hours = FromString<ui64>(offsetDesc.Data() + 1, 2);
            const auto minutes = FromString<ui64>(offsetDesc.Data() + 3, 2);
            const auto result = static_cast<ssize_t>(60 * minutes + SECONDS_IN_HOUR * hours);
            return offsetDesc[0] == '+' ? result : -result;
        }
    }

    TInstant StartOfDay(TInstant value) {
        return value - TDuration::Seconds(value.Seconds() % SECONDS_IN_DAY);
    }

    bool TryParseDateTime(TStringBuf s, TInstant& result) {
        if (TInstant::TryParseIso8601(s, result)) {
            return true;
        }

        auto prefix = [&](TStringBuf p) {
            if (s.StartsWith(p)) {
                s.Skip(p.Size());
                return true;
            }
            return false;
        };

        if (const auto now = TClock::Now(); prefix("now"sv)) {
            result = now;
        } else {
            const auto today = StartOfDay(now);
            if (prefix("today"sv)) {
                result = today;
            } else if (prefix("yesterday"sv)) {
                result = today - TDuration::Days(1);
            } else if (prefix("tomorrow"sv)) {
                result = today + TDuration::Days(1);
            } else {
                result = now;
            }
        }

        while (!s.Empty()) {
            const char sign = s[0];
            if (sign != '+' && sign != '-') {
                return false;
            }
            s.Skip(1);
            const auto durationStr = s.SubString(0, s.find_first_of("+-"sv));
            s.Skip(durationStr.Size());
            TDuration duration;
            if (!TDuration::TryParse(durationStr, duration)) {
                return false;
            }
            result = sign == '+' ? result + duration : result - duration;
        }
        return true;
    }

    TInstant CurrentYearIso8601Local(const TString& s) {
        const auto tzSeconds = GetTimezoneOffsetSeconds();
        const TString formatted = TStringBuilder()
            << TClock::Now().FormatLocalTime("%Y-")
            << s
            << (tzSeconds < 0 ? '-' : '+') << LeftPad(std::abs(tzSeconds) / SECONDS_IN_HOUR, 2, '0')
            << ':'
            << LeftPad(std::abs(tzSeconds) % SECONDS_IN_HOUR, 2, '0');
        return TInstant::ParseIso8601(formatted);
    }

    TInstant AlignToGrid(TDuration period, TInstant now, TFMaybe<TInstant> instant) {
        auto result = instant.GetOrElse(now);
        result -= TDuration::FromValue(result.GetValue() % period.GetValue());
        if (result < now) {
            result += period;
            Y_VERIFY(result >= now, "[%s] >= [%s]", ToString(result).c_str(), ToString(now).c_str());
        }
        return result;
    }
}
