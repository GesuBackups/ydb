#pragma once

#include "baserecords.h"
#include <library/cpp/microbdb/sorterdef.h>

#pragma pack(push, 4)

struct TFromToRec {
    ui32 FromDocId;
    ui32 ToDocId;
    ui16  FromCl;
    ui16  ToCl;

    TFromToRec() {}

    TFromToRec(ui16 fromCl, ui32 fromDocId, ui16 toCl, ui32 toDocId)
        : FromDocId(fromDocId)
        , ToDocId(toDocId)
        , FromCl(fromCl)
        , ToCl(toCl)
    {}

    TFromToRec(const TFromToRec& fio) = default;

    static const ui32 RecordSig = 0x12346400;

    static int ByFrom(const TFromToRec *a, const TFromToRec *b) {

        int comp = (int)a->FromCl - (int)b->FromCl;
        if (!comp)
            comp = a->FromDocId < b->FromDocId ? -1 : a->FromDocId == b->FromDocId ? 0 : 1;
        if (!comp)
            comp = (int)a->ToCl - (int)b->ToCl;
        if (!comp)
            comp = a->ToDocId < b->ToDocId ? -1 : a->ToDocId == b->ToDocId ? 0 : 1;
        return comp;
    }

    static int ByTo(const TFromToRec *a, const TFromToRec *b) {
        int comp = (int)a->ToCl - (int)b->ToCl;
        if (!comp)
            comp = a->ToDocId < b->ToDocId ? -1 : a->ToDocId == b->ToDocId ? 0 : 1;
        return comp;
    }

    static int ByFromOnly(const TFromToRec *a, const TFromToRec *b) {
        int comp = (int)a->FromCl - (int)b->FromCl;
        if (!comp)
            comp = a->FromDocId < b->FromDocId ? -1 : a->FromDocId == b->FromDocId ? 0 : 1;
        return comp;
    }

    static int ByFromDocIdFromCl(const TFromToRec *a, const TFromToRec *b) {
        if (a == b)
            return 0;
        int comp = ::compare(a->FromDocId, b->FromDocId);
        if (!comp)
            comp = ::compare(a->FromCl, b->FromCl);
        return comp;
    }

    bool operator == (const TFromToRec &b) const {
        if (FromDocId != b.FromDocId)
            return false;
        if (ToDocId != b.ToDocId)
            return false;
        if (FromCl != b.FromCl)
            return false;
        if (ToCl != b.ToCl)
            return false;
        return true;
    }

    TFromToRec &operator = (const TFromToRec &b) = default;
};

MAKESORTERTMPL(TFromToRec, ByFrom);
MAKESORTERTMPL(TFromToRec, ByTo);
MAKESORTERTMPL(TFromToRec, ByFromOnly);
MAKESORTERTMPL(TFromToRec, ByFromDocIdFromCl);

struct TFromToHostSigRec : public TFromToRec {
public:
    ui32 HostId;
    ui64 Crc;

public:
    static const ui32 RecordSig = 0x12346401;

public:
    using TFromToRec::ByFrom;

public:
    bool operator == (const TFromToHostSigRec &b) const {
        return memcmp(this, &b, sizeof(TFromToHostSigRec)) == 0;
    }

    TFromToHostSigRec &operator = (const TFromToHostSigRec &b) {
        memcpy(this, &b, sizeof(TFromToHostSigRec));
        return *this;
    }
};

typedef TFromToHostSigRec TFromToHostSig;

MAKESORTERTMPL(TFromToHostSig, ByFrom);

struct TOldNewRec {
    ui32 OldId;
    ui32 NewId;
    static const ui32 RecordSig = 0x12346402;
    TOldNewRec(ui32 oldid, ui32 newid)
        : OldId(oldid)
        , NewId(newid)
    {}
};

struct TDocGroupVecRec : public TDocGroupRec {
    ui32 Vec;
    static const ui32 RecordSig = 0x23456789;
};

MAKESORTERTMPL(TDocGroupVecRec, ByDocGroup);
MAKESORTERTMPL(TDocGroupVecRec, ByGroupDoc);

struct TCrcUidDocRec {
    ui64    Crc;
    ui64    UrlId;
    ui32    HostId;
    ui32    DocId;

    static const ui32 RecordSig = 0x12346403;

    static int ByCrc(const TCrcUidDocRec* a, const TCrcUidDocRec* b) {
        int ret = ::compare(a->HostId, b->HostId);
        ret = ret ? ret : ::compare(a->Crc, b->Crc);
        return ret ? ret : ::compare(a->UrlId, b->UrlId);
    }
};
MAKESORTERTMPL(TCrcUidDocRec, ByCrc);

struct TDocShardScatterRec {
    ui32 DocId;
    ui32 Cluster;
    ui32 Scatter;
    ui32 DocIdOnSearch;

    TDocShardScatterRec(){}
    TDocShardScatterRec(ui32 docId, ui32 cluster, ui32 scatter, ui32 docIdOnSearch)
        : DocId(docId)
        , Cluster(cluster)
        , Scatter(scatter)
        , DocIdOnSearch(docIdOnSearch)
    {
    }

    static const ui32 RecordSig = 0x12346555;

    static int ByDocScatter(const TDocShardScatterRec* lhs, const TDocShardScatterRec* rhs) {
        int ret = ::compare(lhs->DocId, rhs->DocId);
        ret = ret ? ret : ::compare(lhs->Scatter, rhs->Scatter);
        return ret ? ret : ::compare(lhs->Cluster, rhs->Cluster);
    }
};
MAKESORTERTMPL(TDocShardScatterRec, ByDocScatter);

struct TRawScatterPlanRec {
    ui32 FromDocId;
    ui32 ToDocId;
    ui16 ToCl;
    ui16 InDelta;

    static const ui32 RecordSig = 0x12346404;

    TRawScatterPlanRec() {};

    TRawScatterPlanRec(
        const ui32 fromDocId,
        const ui32 toDocId,
        const ui16 toCl,
        const bool inDelta)
        : FromDocId(fromDocId)
        , ToDocId(toDocId)
        , ToCl(toCl)
        , InDelta(inDelta)
        {}

    static int ByFrom(const TRawScatterPlanRec *a, const TRawScatterPlanRec *b) {

        int comp = ::compare(a->FromDocId, b->FromDocId);
        if (!comp)
            comp = (int)a->ToCl - (int)b->ToCl;
        if (!comp)
            comp = a->ToDocId < b->ToDocId ? -1 : a->ToDocId == b->ToDocId ? 0 : 1;
        return comp;
    }

    static int ByTo(const TRawScatterPlanRec *a, const TRawScatterPlanRec *b) {
        int comp = (int)a->ToCl - (int)b->ToCl;
        if (!comp)
            comp = a->ToDocId < b->ToDocId ? -1 : a->ToDocId == b->ToDocId ? 0 : 1;
        return comp;
    }

    static int ByFromOnly(const TRawScatterPlanRec *a, const TRawScatterPlanRec *b) {
        return ::compare(a->FromDocId, b->FromDocId);
    }
};

MAKESORTERTMPL(TRawScatterPlanRec, ByFrom);
MAKESORTERTMPL(TRawScatterPlanRec, ByFromOnly);
MAKESORTERTMPL(TRawScatterPlanRec, ByTo);

struct TCrcZoneHitsRec {
    ui32 Crc;
    ui32 Zone;
    ui64 Hits;

    TCrcZoneHitsRec() {}
    TCrcZoneHitsRec(ui32 crc, ui32 zone, ui64 hits)
        : Crc(crc)
        , Zone(zone)
        , Hits(hits)
    {
    }

    static const ui32 RecordSig = 0x12346405;

    static int ByCrcZone(const TCrcZoneHitsRec* lhs, const TCrcZoneHitsRec* rhs) {
        int ret;
        if (ret = ::compare(lhs->Crc, rhs->Crc)) {
            return ret;
        }
        if (ret = ::compare(lhs->Zone, rhs->Zone)) {
            return ret;
        }
        return ::compare(lhs->Hits, rhs->Hits);
    }

    bool SameCrcZone(const TCrcZoneHitsRec* rec) const {
        return Crc == rec->Crc && Zone == rec->Zone;
    }

    void AddHits(const TCrcZoneHitsRec* rec) {
        Hits += rec->Hits;
    }
};

MAKESORTERTMPL(TCrcZoneHitsRec, ByCrcZone);

struct TSearchSortRec {
    ui32    DocId;
    ui32    WalrusIndex; // for writing doc_scatter_shard.dat
    ui32    DocIdOnWalrus; // for writing doc_scatter_shard.dat
    union {
        float   PruningF;
        ui16    PruningI;
    };
    ui16    Cluster;
    ui8     PruningGroup;
    url_t   RevHostName;

    static const ui32 RecordSig = 0x65432101;

    size_t SizeOf() const {
        return offsetof(TSearchSortRec, RevHostName) + strlen(RevHostName) + 1;
    }

    static int SearchOrder(const TSearchSortRec* a, const TSearchSortRec* b) {
        int cmp = (int)a->PruningGroup - (int)b->PruningGroup;
        if (cmp)
            return cmp;
        cmp = stricmp(a->RevHostName, b->RevHostName);
        if (cmp)
            return cmp;
        return (int)a->DocId - (int)b->DocId;
    }

    static int PruningOrder(const TSearchSortRec* a, const TSearchSortRec* b) {
        return b->PruningF > a->PruningF ? 1 : b->PruningF < a->PruningF ? -1 : a->DocIdOnWalrus > b->DocIdOnWalrus ? 1 : a->DocIdOnWalrus < b->DocIdOnWalrus ? -1 : 0;
    }
};

MAKESORTERTMPL(TSearchSortRec, SearchOrder);
MAKESORTERTMPL(TSearchSortRec, PruningOrder);


#pragma pack(pop)
