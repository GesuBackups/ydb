#pragma once

#include <util/stream/output.h>
#include <util/stream/input.h>

namespace NYql::NUdf {

namespace NLz4 {

enum class EStreamType {
    Unknown = 0,
    Frame,
    Legacy
};

EStreamType CheckMagic(const void* data);

void DecompressFrame(IInputStream* input, IOutputStream* output);
void DecompressLegacy(IInputStream* input, IOutputStream* output);
}

}

