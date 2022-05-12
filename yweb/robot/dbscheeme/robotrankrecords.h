#pragma once

#include "dbtypes.h"
#include <library/cpp/microbdb/sorterdef.h>

#pragma pack(push, 4)

struct TSelRankIO {
    ui32 HostId;
    ui64 UrlId;
    double Rank;
    ui32 Zone;
    static const ui32 RecordSig = 0x56211F06;

    TSelRankIO()
        : HostId(0)
        , UrlId(0)
        , Rank(0)
        , Zone(0)
    {}


    TSelRankIO(ui32 hostid, ui64 urlid, double rank, ui32 zone)
        : HostId(hostid)
        , UrlId(urlid)
        , Rank(rank)
        , Zone(zone)
    {}

    static int ByHostUrlId(const TSelRankIO* u1, const TSelRankIO* u2) {
        int cmp = ::compare(u1->HostId, u2->HostId);
        return cmp ? cmp : ::compare(u1->UrlId, u2->UrlId);
    }

    static int ByRank(const TSelRankIO* u1, const TSelRankIO* u2) {
        return -(::compare(u1->Rank, u2->Rank));
    }

    static int ByUid(const TSelRankIO* u1, const TSelRankIO* u2)
    {
        return CompareUids(u1, u2);
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }
};

MAKESORTERTMPL(TSelRankIO, ByHostUrlId);
MAKESORTERTMPL(TSelRankIO, ByRank);
MAKESORTERTMPL(TSelRankIO, ByUid);

struct TUidSelRankRec {
    ui32 HostId;
    ui64 UrlId;
    double Rank;
    ui32 FormulaId;
    static const ui32 RecordSig = 0x56211F07;

    TUidSelRankRec()
        : HostId(0)
        , UrlId(0)
        , Rank(0)
        , FormulaId(0)
    {}


    TUidSelRankRec(ui32 hostId, ui64 urlId, double rank, ui32 formulaId)
        : HostId(hostId)
        , UrlId(urlId)
        , Rank(rank)
        , FormulaId(formulaId)
    {}

    static int ByRank(const TUidSelRankRec* u1, const TUidSelRankRec* u2) {
        return -(::compare(u1->Rank, u2->Rank));
    }

    static int ByUid(const TUidSelRankRec* u1, const TUidSelRankRec* u2)
    {
        return CompareUids(u1, u2);
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }
};

MAKESORTERTMPL(TUidSelRankRec, ByRank);
MAKESORTERTMPL(TUidSelRankRec, ByUid);

struct TBagRankRec {
    ui64 UrlId;
    double Rank;
    ui32 HostId;
    ui32 Zone;
    ui32 DocHits;
    static const ui32 RecordSig = 0x56222F09;

    TBagRankRec()
        : UrlId(0)
        , Rank(0)
        , HostId(0)
        , Zone(0)
        , DocHits(0)
    {}


    TBagRankRec(ui32 hostid, ui64 urlid, double rank, ui32 docHits, ui32 zone)
        : UrlId(urlid)
        , Rank(rank)
        , HostId(hostid)
        , Zone(zone)
        , DocHits(docHits)
    {}

    static int ByRank(const TBagRankRec* u1, const TBagRankRec* u2) {
        return -(::compare(u1->Rank, u2->Rank));
    }

    static int ByUid(const TBagRankRec* u1, const TBagRankRec* u2)
    {
        return CompareUids(u1, u2);
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }
};

MAKESORTERTMPL(TBagRankRec, ByRank);
MAKESORTERTMPL(TBagRankRec, ByUid);

struct TUploadRankRec {
    ui32 Cluster;
    ui32 DocId;
    double Rank;
    ui32 UploadRuleId;
    ui8 ToUpload;
    static const ui32 RecordSig = 0x56311F08;

    TUploadRankRec(ui32 cluster, ui32 docId, double rank, ui32 uploadRuleId, bool toUpload)
        : Cluster(cluster)
        , DocId(docId)
        , Rank(rank)
        , UploadRuleId(uploadRuleId)
        , ToUpload(toUpload)
    {}

    static int ByClusterDocIdUploadRule(const TUploadRankRec* u1, const TUploadRankRec* u2) {
        int cmp = ::compare(u1->Cluster, u2->Cluster);
        if (cmp) return cmp;
        cmp = ::compare(u1->DocId, u2->DocId);
        if (cmp) return cmp;
        return ::compare(u1->UploadRuleId, u2->UploadRuleId);
    }

};

MAKESORTERTMPL(TUploadRankRec, ByClusterDocIdUploadRule);

struct TClusterDocId {
    ui32 Cluster;
    ui32 DocId;

    static const ui32 RecordSig = 0x56311F07;

    TClusterDocId()
        : Cluster(0)
        , DocId(0)
    {}

    TClusterDocId(ui32 cluster, ui32 docId)
        : Cluster(cluster)
        , DocId(docId)
    {}

    TClusterDocId(const TUploadRankRec& rec)
        : Cluster(rec.Cluster)
        , DocId(rec.DocId)
    {}

    bool operator < (const TClusterDocId& o) const {
        return Cluster < o.Cluster|| (Cluster == o.Cluster && DocId < o.DocId);
    }
};

struct TPageRankRec {
    double PageRank;
    ui64 UrlId;
    ui32 HostId;

    static const ui32 RecordSig = 0x56222F07;

    TPageRankRec(ui32 hostId, ui64 urlId, double pageRank)
        : PageRank(pageRank)
        , UrlId(urlId)
        , HostId(hostId)
    {}

    static int ByUid(const TPageRankRec* u1, const TPageRankRec* u2)
    {
        return CompareUids(u1, u2);
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }
};

MAKESORTERTMPL(TPageRankRec, ByUid);

struct TDocUidRank {
    ui32 DocId;
    ui32 HostId;
    ui64 UrlId;
    double Rank;
    ui32 HitsCount;
    ui32 TagDocSize;
    ui32 PlainDocSize;
    ui8 Zone;

    static const ui32 RecordSig = 0x18038709;

    TDocUidRank(ui32 docid, ui32 hostid, ui64 urlid, double rank, ui32 hitsCount, ui32 tagDocSize, ui32 plainDocSize, ui8 zone)
        : DocId(docid)
        , HostId(hostid)
        , UrlId(urlid)
        , Rank(rank)
        , HitsCount(hitsCount)
        , TagDocSize(tagDocSize)
        , PlainDocSize(plainDocSize)
        , Zone(zone)
    {}

    static int ByDoc(const TDocUidRank* u1, const TDocUidRank* u2) {
        return ::compare(u1->DocId, u2->DocId);
    }

    static int ByRank(const TDocUidRank* u1, const TDocUidRank* u2) {
        return -(::compare(u1->Rank, u2->Rank));
    }

    static int ByRankDoc(const TDocUidRank* u1, const TDocUidRank* u2) {
        int rankCmp = ByRank(u1, u2);
        return rankCmp ? rankCmp : ByDoc(u1, u2);
    }
};

MAKESORTERTMPL(TDocUidRank, ByDoc);
MAKESORTERTMPL(TDocUidRank, ByRank);
MAKESORTERTMPL(TDocUidRank, ByRankDoc);

struct TUidVerdict {
    ui32 HostId;
    ui64 UrlId;
    ui32 Verdict;
    static const ui32 RecordSig = 0x18038702;

    TUidVerdict(ui32 hostid, ui64 urlid, ui32 verdict)
        : HostId(hostid), UrlId(urlid), Verdict(verdict) {}

    static int ByUid(const TUidVerdict* u1, const TUidVerdict* u2)
    {
        return CompareUids(u1, u2);
    }
};

MAKESORTERTMPL(TUidVerdict, ByUid);

struct TNameRank {
    double Rank;
    url_t Name;

    static const ui32 RecordSig = 0x18038703;

    size_t SizeOf() const {
        return offsetof(TNameRank, Name)+strlen(Name)+1;
    }

    static int ByName(const TNameRank* u1, const TNameRank* u2)
    {
        return strcmp(u1->Name, u2->Name);
    }
};

MAKESORTERTMPL(TNameRank, ByName);

struct THostIdName {
    ui32 HostId;
    url_t Name;

    static const ui32 RecordSig = 0x18038704;

    size_t SizeOf() const {
        return offsetof(THostIdName, Name)+strlen(Name)+1;
    }

    static int ByHostIdDir(const THostIdName* u1, const THostIdName* u2) {
        int cmp;
        return (cmp = ::compare(u1->HostId, u2->HostId)) ? cmp :  ByDir(u1, u2);
    }

    static int ByName(const THostIdName* u1, const THostIdName* u2) {
        return strcmp(u1->Name, u2->Name);
    }

    static int ByDir(const THostIdName* u1, const THostIdName* u2)
    {
        const char* short1 = strchr(u1->Name, '/');
        const char* short2 = strchr(u2->Name, '/');
        const char* last1 = strrchr(u1->Name, '/');
        const char* last2 = strrchr(u2->Name, '/');
        char* dir1 = new char[last1-short1+2];
        char* dir2 = new char[last2-short2+2];
        strncpy(dir1, short1, last1-short1+1);
        strncpy(dir2, short2, last2-short2+1);
        dir1[last1-short1+1] = '\0';
        dir2[last2-short2+1] = '\0';
        int cmpr = strcmp(dir1, dir2);
        int ret = ((cmpr > 0) ? 1 : ((cmpr < 0) ? -1 : 0));
        delete[] dir1;
        delete[] dir2;
        return ret;

        /*TString url1(u1->Name);
        TString url2(u2->Name);
        size_t first = url1.find_first_of('/');
        TString dir1 = url1.substr(first, url1.find_last_of('/')-first+1);
        first = url2.find_first_of('/');
        TString dir2 = url2.substr(first, url2.find_last_of('/')-first+1);
        if (dir1 > dir2)
            return 1;
        else if (dir1 < dir2)
            return -1;
        else
            return strcmp(u1->Name, u2->Name);*/
    }
};

MAKESORTERTMPL(THostIdName, ByHostIdDir);
MAKESORTERTMPL(THostIdName, ByDir);
MAKESORTERTMPL(THostIdName, ByName);

struct TNameRec {
    url_t Name;

    static const ui32 RecordSig = 0x18038706;

    size_t SizeOf() const {
        return offsetof(THostIdName, Name)+strlen(Name)+1;
    }
    static int ByName(const TNameRec* u1, const TNameRec* u2)
    {
        return strcmp(u1->Name, u2->Name);
    }
};

MAKESORTERTMPL(TNameRec, ByName);

struct TFactorsRec {
    double Rank;
    ui32 PR;
    ui32 PRext;
    ui8 Hops;
    url_t Name;

    static const ui32 RecordSig = 0x18038707;

    size_t SizeOf() const {
        return offsetof(TFactorsRec, Name)+strlen(Name)+1;
    }

    static int ByName(const TFactorsRec* u1, const TFactorsRec* u2)
    {
        return strcmp(u1->Name, u2->Name);
    }

    static int ByDir(const TFactorsRec* u1, const TFactorsRec* u2)
    {
        const char* short1 = strchr(u1->Name, '/');
        const char* short2 = strchr(u2->Name, '/');
        const char* last1 = strrchr(u1->Name, '/');
        const char* last2 = strrchr(u2->Name, '/');
        char* dir1 = new char[last1-short1+2];
        char* dir2 = new char[last2-short2+2];
        strncpy(dir1, short1, last1-short1+1);
        strncpy(dir2, short2, last2-short2+1);
        dir1[last1-short1+1] = '\0';
        dir2[last2-short2+1] = '\0';
        int cmpr = strcmp(dir1, dir2);
        int ret = ((cmpr > 0) ? 1 : ((cmpr < 0) ? -1 : 0));
        delete[] dir1;
        delete[] dir2;
        return ret;
/*
        TString url1(u1->Name);
        TString url2(u2->Name);
        size_t first = url1.find_first_of('/');
        TString dir1 = url1.substr(first, url1.find_last_of('/')-first+1);
        first = url2.find_first_of('/');
        TString dir2 = url2.substr(first, url2.find_last_of('/')-first+1);
        if (dir1 > dir2)
            return 1;
        else if (dir1 < dir2)
            return -1;
        else
            return ByName(u1, u2);*/
    }
};

MAKESORTERTMPL(TFactorsRec, ByName);
MAKESORTERTMPL(TFactorsRec, ByDir);

struct TFactRec {
    ui32 HostId;
    ui64 UrlId;
    double PageRank;
    double TextFeatures;
    double Long;
    double AddTime;
    double YaBar;
    double NewLinkQuality;
    double Nevasca2;
    double DocLen;

    static const ui32 RecordSig = 0x18038708;

    static int ByUid(const TFactRec* u1, const TFactRec* u2)
    {
        return CompareUids(u1, u2);
    }
};

MAKESORTERTMPL(TFactRec, ByUid);

struct TUidNumber {
    ui64 UrlId;
    ui32 HostId;
    ui32 Number;

    static const ui32 RecordSig = 0x43255793;

    static int ByNumber(const TUidNumber* a, const TUidNumber* b) {
        return ::compare(a->Number, b->Number);
    }
};

MAKESORTERTMPL(TUidNumber, ByNumber);

struct THostFactRec {
    ui32 HostId;

    double Bookmarks;
    double BadBookmarks;
    double BookmarkUsers;
    double HostRank;
    double NewsCit;
    double Spam2;
    double Nevasca1;
    double Nevasca2;
    double LiruOutTraffic1;
    double LiruOutTraffic2;
    double YabarHostVisits;
    double YaBarCoreHost;
    double YaBarCoreOwner;
    double YabarHostSearchTraffic;
    double YabarHostInternalTraffic;
    double YabarHostAvgTime;
    double YabarHostAvgTime2;
    double YabarHostAvgActions;
    double YabarHostBrowseRank;
    double OwnerClicksPCTR;
    double OwnerSDiffClickEntropy;
    double OwnerSDiffShowEntropy;
    double OwnerSDiffCSRatioEntropy;
    double OwnerSessNormDur;
    double Tic;
    double HasTic;
    double NoSpam;
    double YaBar;
    double Adv;

    static const ui32 RecordSig = 0xA574460B;

    static int ByHostId(const THostFactRec *a, const THostFactRec *b) {
        return compare(a->HostId, b->HostId);
    }
};

MAKESORTERTMPL(THostFactRec, ByHostId);

struct TRobotRankFactorsTemplate {
    ui32 HostId;
    ui64 UrlId;

    double PR;
    double PRExt;
    double PRDMoz;
    double CR;
    double CRExt;
    double Weight;
    double Hops;

    double ShowClickEx;
    double ClicksDay;
    double ClicksWeek;
    double ShowsDay;
    double ShowsWeek;

    ui32 DirId;
    ui32 ParamId;
    ui32 StoneId;
    ui32 Zone;
    ui32 HttpCode;

    ui8 Status;
    ui8 Language;
    ui8 ToDump;
    ui8 Dummy1; // Needed due to the 32-bit field alignment

    ui32 ExpInfo;

    url_t Name;

    static const ui32 RecordSig = 0x43CCE7B1;

    size_t SizeOf() const {
        return offsetof(TRobotRankFactorsTemplate, Name) + strlen(Name) + 1;
    }
};

struct TRecrawlRankFactorsBase {
    // Store only float fields!!!
    float ChangeFreq;
    float BigChangeFreq;
    float TimeLastChange;
    float TimeLastBigChange;
    float LcAverageChange;
    float LcMaxChange;
    float LcAverageChangeSpeed;
    float LcMaxChangeSpeed;
    float LcFreshnessRateCastillo;
    float LcFreshnessRateCastilloBigChange;
    float Variability;
    float OldVariability;
    float SmallChangeRate;
    float NormalChangeRate;
    float BigChangeRate;
    float Correlation;
    float HasEmptyShingle;
    float HistoryLength;

    static const ui32 RecordSig = 0x43DCF7B1;

    TRecrawlRankFactorsBase() {
        memset(this, 0, sizeof(TRecrawlRankFactorsBase));
    }

    const TRecrawlRankFactorsBase& operator+= (const TRecrawlRankFactorsBase& rhs) {
        float* begin = (float*)this;
        float* end = begin + sizeof(*this) / sizeof(float);
        const float* rhsBegin = (const float*)&rhs;

        while (begin != end) {
            *begin += *rhsBegin;
            ++begin;
            ++rhsBegin;
        }

        return *this;
    }
    void DivideAll(int value) {
        if (value == 0) {
            return;
        }
        float* begin = (float*)this;
        float* end = begin + sizeof(*this) / sizeof(float);

        while (begin != end) {
            *begin /= value;
            ++begin;
        }
    }
    void MultLastCrawl(ui32 lastCrawl) {
        LcAverageChange *= lastCrawl;
        LcMaxChange *= lastCrawl;
        LcAverageChangeSpeed *= lastCrawl;
        LcMaxChangeSpeed *= lastCrawl;
        LcFreshnessRateCastillo *= lastCrawl;
        LcFreshnessRateCastilloBigChange *= lastCrawl;
    }
};

struct TRecrawlRankFactorsTemplate : public TRecrawlRankFactorsBase {
    ui32 HostId;
    ui64 UrlId;

    ui32 DirId;
    ui32 ParamId;
    ui32 StoneId;
    ui32 Zone;

    ui8 Status;
    ui8 Hops;
    ui8 Dummy1; ui8 Dummy2; // Needed due to the 32-bit field alignment
    ui32 ExpInfo;
    ui32 Size;
    ui32 Addtime;
    ui32 PathLen;
    ui32 LastCrawl;

    url_t DumpedLastShingle;

    static const ui32 RecordSig = 0x43DDF7B1;
};

struct TNotFoundRankFactorsTemplate {
    ui32 HostId;
    ui64 UrlId;

    ui8 Status;
    ui8 Dummy1; ui8 Dummy2; ui8 Dummy3;

    ui32 DirId;
    ui32 ParamId;
    ui32 StoneId;

    static const ui32 Zone = 0;
    static const ui32 CountParam = 0;
    static const ui32 RecordSig = 0x42146782;

    ui32 ExpInfo;
};

struct TNotFoundRankFactors {
    ui32 HostId;
    ui64 UrlId;

    ui32 Host_OK3;
    ui32 Host_OK7;
    ui32 Host_NF3;
    ui32 Host_NF7;
    ui32 Param_OK3;
    ui32 Param_OK7;
    ui32 Param_NF3;
    ui32 Param_NF7;
    ui32 Dir_OK3;
    ui32 Dir_OK7;
    ui32 Dir_NF3;
    ui32 Dir_NF7;

    static const ui32 RecordSig = 0xD00BA001;

    static int ByUid(const TNotFoundRankFactors* u1, const TNotFoundRankFactors* u2)
    {
        return CompareUids(u1, u2);
    }
};

MAKESORTERTMPL(TNotFoundRankFactors, ByUid);

struct TNotFoundRankLearnFactors {
    ui32 HostId;
    ui64 UrlId;
    ui32 Status;

    float Host_OK3;
    float Host_OK7;
    float Host_NF3;
    float Host_NF7;
    float Param_OK3;
    float Param_OK7;
    float Param_NF3;
    float Param_NF7;
    float Dir_OK3;
    float Dir_OK7;
    float Dir_NF3;
    float Dir_NF7;
    float IntLinks;
    float NewLinks;
    float VanishedLinks;
    float AvgLinkAge;
    float LastAccess;
    float TimeInDb;
    float NoSitemap;
    float InSitemap;
    float NumberOfLinks;

    static const ui32 RecordSig = 0xD00BC102;

    static int ByUid(const TNotFoundRankLearnFactors* u1, const TNotFoundRankLearnFactors* u2)
    {
        return CompareUids(u1, u2);
    }
};

MAKESORTERTMPL(TNotFoundRankLearnFactors, ByUid);

#pragma pack(pop)
