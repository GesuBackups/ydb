#pragma once

#include <logbroker/unified_agent/common/util/clock.h>
#include <logbroker/unified_agent/common/util/f_maybe.h>

namespace NUnifiedAgent {
    TInstant StartOfDay(TInstant value);

    bool TryParseDateTime(TStringBuf s, TInstant& result);

    TInstant CurrentYearIso8601Local(const TString& s);

    TInstant AlignToGrid(TDuration period, TInstant now = TClock::Now(), TFMaybe<TInstant> instant = Nothing());
}
