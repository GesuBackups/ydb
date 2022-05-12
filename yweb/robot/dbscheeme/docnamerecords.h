#pragma once

#include "dbtypes.h"
#include <library/cpp/microbdb/sorterdef.h>

#pragma pack(push, 4)

struct TDocName {
    ui32   DocId;
    url_t  Name;

    static const ui32 RecordSig = 0xB4621608;

    size_t SizeOf() const {
        return offsetof(TDocName, Name) + strlen(Name) + 1;
    }

    static int ByDocId(const TDocName *a, const TDocName *b) {
        return ::compare(a->DocId, b->DocId);
    }
};

MAKESORTERTMPL(TDocName, ByDocId);

struct TSegmentDocName {
    ui32   DocId;
    ui32   Segment;
    url_t  Name;

    static const ui32 RecordSig = 0xB4621609;

    size_t SizeOf() const {
        return offsetof(TSegmentDocName, Name) + strlen(Name) + 1;
    }
};

#pragma pack(pop)
