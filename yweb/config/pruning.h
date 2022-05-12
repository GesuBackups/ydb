#pragma once

#include <util/system/defaults.h>
#include <util/generic/vector.h>

// Group boundaries for pruning used in Robot, Oxygen and FastTier.
static const ui16 PruningGroupBoundaries[] = {
    512, 1024, 1536, 2048, 2560, 3072, 3584, 4096, 5120, 6144, 7168, 8192,
    10240, 12288, 14336, 16384,
    20480, 24576, 28672, 32768,
    40960, 49152, 57344, 65535
};

inline
const TVector<ui16>
GetPruningGroupBoundaries() {
    TVector<ui16> ret(PruningGroupBoundaries,
                      PruningGroupBoundaries
                      + sizeof(PruningGroupBoundaries) / sizeof(ui16));
    return ret;
}
