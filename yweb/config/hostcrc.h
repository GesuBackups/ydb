#pragma once

#include <string.h>
#include <util/digest/fnv.h>
#include <util/system/maxlen.h>
#include <util/system/compat.h>
#include <util/system/defaults.h>
#include <util/generic/yexception.h>

enum EDom2ProcStyle {
    D2PS_LimitedSpread = 1,
    D2PS_FullSpread    = 2,
};

ui64 HostCrc4Config(TStringBuf Name, const char* config = nullptr);
