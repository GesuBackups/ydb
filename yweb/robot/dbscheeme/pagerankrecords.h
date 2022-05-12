#pragma once

#include "dbtypes.h"
#include <yweb/protos/robot.pb.h>
#include <util/system/defaults.h>

class TPagerankResults;

#pragma pack(push, 4)

struct TNamedResults {
    typedef TPagerankResults TExtInfo;

    ui64    UrlId;
    ui32    HostId;
    char    HostName[1024];

    static const ui32  RecordSig = 0x2349FF07;

    size_t SizeOf() const {
        return offsetof(TNamedResults, HostName) + strlen(HostName) + 1;
    }

    static int ByUid(const TNamedResults* a, const TNamedResults* b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TNamedResults, ByUid);

struct TTransitionResult {
    ui32    HostId;
    ui64    UrlId;
    ui64    DstHostFnv;
    ui32    DstHostId;
    ui64    DstUrlId;
    ui32    DstSegment;

    static const ui32  RecordSig = 0x2349FF08;

    static int ByUid(const TTransitionResult* a, const TTransitionResult* b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TTransitionResult, ByUid);

struct TSubstRec {
    ui32    SrcHostId;
    ui32    DstHostId;
    ui64    SrcUrlId;
    ui64    DstUrlId;
    ui8     IsRedirect;

    static const ui32  RecordSig = 0x2349FF09;

    static int BySrcUid(const TSubstRec* a, const TSubstRec* b) {
        int ret = ::compare(a->SrcHostId, b->SrcHostId);
        return ret ? ret : ::compare(a->SrcUrlId, b->SrcUrlId);
    }
};

MAKESORTERTMPL(TSubstRec, BySrcUid);

struct TLinkInfo {
    ui32 HostId;
    ui64 LinksCount;

    static const ui32  RecordSig = 0x2349FF10;

    static int ByUid(const TLinkInfo* a, const TLinkInfo* b) {
        return ::compare(a->HostId, b->HostId);
    }
};

MAKESORTERTMPL(TLinkInfo, ByUid);

struct TShowsRec {
    ui64 UrlId;
    ui32 HostId;
    ui32 ClicksDay;
    ui32 ShowsDay;
    ui32 ClicksWeekly;
    ui32 ShowsWeekly;

    static const ui32  RecordSig = 0x2349FF11;

    static int ByUid(const TShowsRec* a, const TShowsRec* b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TShowsRec, ByUid);

#pragma pack(pop)
