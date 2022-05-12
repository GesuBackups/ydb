#pragma once
#include <library/cpp/microbdb/sorterdef.h>

#include "dbtypes.h"

namespace NFetcherMsg {
class TFetchRequest;
}

namespace NRealTime {
class THostFactors;
}

struct TPbUrlQuel {
    static const ui32 RecordSig = 0x12349801;
    typedef NFetcherMsg::TFetchRequest TExtInfo;
    size_t SizeOf() const {
        return 0;
    }
    static int BySerialized(const TPbUrlQuel *a, const TPbUrlQuel *b);
    static int ByUrl(const TPbUrlQuel *a, const TPbUrlQuel *b);
};

MAKESORTERTMPL(TPbUrlQuel, BySerialized);
MAKESORTERTMPL(TPbUrlQuel, ByUrl);

struct THostFactorRec {
    static const ui32 RecordSig = 0x12349802;
    typedef NRealTime::THostFactors TExtInfo;
    size_t SizeOf() const {
        return 0;
    }
};

class TFnvDstData;
struct TFnvDstDataRec {
    static const ui32 RecordSig = 0x12349803;
    typedef TFnvDstData TExtInfo;
    size_t SizeOf() const {
        return 0;
    }
};

#pragma pack(push, 4)
struct TFnvDstDataNPRec {
    static const ui32 RecordSig = 0x12349804;
    ui64 Fnv;
    ui64 TrueDstHostFnv;
    double SourceRank;
    ui32 Dst;
    ui32 Src;
    ui32 SrcHost;
    ui32 SrcOwner;
    ui32 LFlags;
    ui32 BFlags;
    ui32 SeoFlags;
    ui32 Pr;
    ui32 Flags;
    ui32 Date;
    ui32 Lang;
    ui32 NationalDomainId;
    ui32 SrcDate;
    ui32 GroupSize;
};
#pragma pack(pop)

class TAura;
struct TAuraRec {
    static const ui32 RecordSig = 0x13338771;
    typedef TAura TExtInfo;

    ui64 Owner;
    ui64 Path;

    TAuraRec(ui64 owner = 0, ui64 path = 0)
        : Owner(owner)
        , Path(path)
    {
    }

    size_t SizeOf() const {
        return 2 * sizeof(ui64);
    }
};

struct TOwnerAuraRec {
    static const ui32 RecordSig = 0x13338772;
    typedef TAura TExtInfo;

    dbscheeme::host_t OwnerName;

    size_t SizeOf() const {
        return strlen(OwnerName) + 1;
    }
};
