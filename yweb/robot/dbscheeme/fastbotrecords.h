#pragma once

#include "dbtypes.h"
#include <library/cpp/microbdb/sorterdef.h>

#pragma pack(push, 4)

struct TFBLinkData {
    ui64        Fnv;            // Fnv-hash текста
    ui32        DocId;          // Целевой docid (кластерный)
    ui32        SrcId;          // Источника docid (как в url.dat)
    ui32        SrcHost;        // OwnerId
    ui32        UrlFlags;       // Флаги из url.dat
    ui32        LinkFlags;      // Флаги из таблиц ссылок
    ui32        Tic;            // Тиц источника
    ui32        Pr;             // PageRank
    ui16        DateDiscovered; //
    ui8         Language;       // Язык источника
    linktext_t  Text;           // Текст ссылки

    static const ui32 RecordSig = 0xFA1290CB;

    static int ByFnv(const TFBLinkData* a, const TFBLinkData* b) {
        return compare(a->Fnv, b->Fnv);
    }

    static int ByUid(const TFBLinkData* a, const TFBLinkData* b) {
        int ret = compare(a->SrcId, b->SrcId);
        return ret ? ret : compare(a->Fnv, b->Fnv);
    }

    static int ByDocIdFnv(const TFBLinkData* a, const TFBLinkData* b) {
        int ret = compare(a->DocId, b->DocId);
        ret = ret ? ret : compare(a->Fnv, b->Fnv);
        ret = ret ? ret : compare(a->SrcHost, b->SrcHost);
        return ret ? ret : compare(a->SrcId, b->SrcId);
    }

    size_t SizeOf() const {
        return offsetof(TFBLinkData, Text) + strlen(Text) + 1;
    }
};


MAKESORTERTMPL(TFBLinkData, ByFnv);
MAKESORTERTMPL(TFBLinkData, ByUid);
MAKESORTERTMPL(TFBLinkData, ByDocIdFnv);


struct TTempFastGoogleRec {
    ui64    UrlIdFrom;
    ui64    UrlIdTo;
    ui64    Fnv;
    ui32    HostIdFrom;
    ui32    HostIdTo;
    ui32    DocIdFrom;
    ui32    LinkFlags;
    ui16    DateDiscovered;


    static const ui32 RecordSig = 0xFA1290CD;

public:
    static int ByDstSrc(const TTempFastGoogleRec *a, const TTempFastGoogleRec *b) {
        int ret = compare(a->HostIdTo, b->HostIdTo);
        ret = ret ? ret : compare(a->UrlIdTo, b->UrlIdTo);
        ret = ret ? ret : compare(a->HostIdFrom, b->HostIdFrom);
        return ret ? ret : compare(a->UrlIdFrom, b->UrlIdFrom);
    }

    static int BySrcUid(const TTempFastGoogleRec* a, const TTempFastGoogleRec* b) {
        int ret = compare(a->HostIdFrom, b->HostIdFrom);
        return ret ? ret : compare(a->UrlIdFrom, b->UrlIdFrom);
    }

public:
    ui32 GetHostIdFrom() const {
        return HostIdFrom;
    }
};

MAKESORTERTMPL(TTempFastGoogleRec, ByDstSrc);
MAKESORTERTMPL(TTempFastGoogleRec, BySrcUid);

struct THostRankRec {
    typedef double TRankType;
    TRankType   Rank;
    dbscheeme::host_t      Name;

    static const ui32 RecordSig = 0x194D5ED8;

    inline THostRankRec()
        : Rank(0.0)
    {
        Name[0] = '\0';
    }

    inline THostRankRec(TRankType const& rank, char const* name) {
        Assign(rank, name);
    }

    size_t SizeOf() const {
        return offsetof(THostRankRec, Name) + strlen(Name) + 1;
    }

    inline void SetName(char const* name) {
        strlcpy(Name, name, sizeof(Name));
    }

    inline void Assign(TRankType const& rank, char const* name) {
        SetName(name);
        Rank = rank;
    }

    static int ByName(THostRankRec const *a, THostRankRec const *b) {
        return stricmp(a->Name, b->Name);
    }
};

MAKESORTERTMPL(THostRankRec, ByName);

struct TLinkDCR {
    typedef double TRankType;
    ui32      DocId;
    ui32      LinkNum;
    TRankType DeltaCrawlRank;

    TLinkDCR()
        : DocId(0)
        , LinkNum(0)
        , DeltaCrawlRank(TRankType())
    {
    }

    TLinkDCR(ui32 const docId, ui32 const linkNum = 0, TRankType const deltaCrawlRank = TRankType())
        : DocId(docId)
        , LinkNum(linkNum)
        , DeltaCrawlRank(deltaCrawlRank)
    {
    }

    static const ui32 RecordSig = 0xABDA54AE;

    static int ByDefault(TLinkDCR const *a, TLinkDCR const *b) {
        int ret = compare(a->DocId, b->DocId);
        return ret ? ret : compare(a->LinkNum, b->LinkNum);
    }
};

MAKESORTERTMPL(TLinkDCR, ByDefault);

struct TDocCrawlRanks {
    typedef double TRankType;
    ui32      DocId;
    TRankType HostCrawlRank;
    TRankType DocCrawlRank;

    static const ui32 RecordSig = 0x93857385;

    TDocCrawlRanks()
        : DocId(0)
        , HostCrawlRank(TRankType())
        , DocCrawlRank(TRankType())
    {
    }

    TDocCrawlRanks(ui32 const docId, TRankType const hostCrawlRank = TRankType(), TRankType const docCrawlRank = TRankType())
        : DocId(docId)
        , HostCrawlRank(hostCrawlRank)
        , DocCrawlRank(docCrawlRank)
    {
    }
};


struct TDocCrawlRanksPr : public TDocCrawlRanks {
    double HostPR;
    double DocPR;

    static const ui32 RecordSig = 0x93557388;

    TDocCrawlRanksPr()
        : TDocCrawlRanks()
        , HostPR(double())
        , DocPR(double())
    {
    }

    TDocCrawlRanksPr(ui32 const docId, TRankType const hostCrawlRank = TRankType(), TRankType const docCrawlRank = TRankType(), double const hostPR = double(), double const docPR = double())
        : TDocCrawlRanks(docId, hostCrawlRank, docCrawlRank)
        , HostPR(hostPR)
        , DocPR(docPR)
    {
    }

    TDocCrawlRanksPr(TDocCrawlRanks const& docCrawlRanks, double const hostPR = double(), double const docPR = double())
        : TDocCrawlRanks(docCrawlRanks)
        , HostPR(hostPR)
        , DocPR(docPR)
    {
    }
};


struct TFastDocDate {
public:
    ui32 DocId = 0;
    ui32 Date = 0;

    static const ui32 RecordSig = 0x2EAA2FCC;

    TFastDocDate() {}
    TFastDocDate(ui32 const docId, ui32 const date)
        : DocId(docId)
        , Date(date)
    {
    }
};

#pragma pack(pop)
