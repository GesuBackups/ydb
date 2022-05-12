#pragma once

#include "baserecords.h"
#include "shinglerecords.h"
#include "docnamerecords.h"
#include "urlflags.h"
#include <library/cpp/microbdb/sorterdef.h>
#include <library/cpp/mime/types/mime.h>
#include <library/cpp/charset/doccodes.h>
#include <yweb/config/langregion.h>
#include <kernel/langregion/langregion.h>
#include <yweb/protos/roboturlatrs.pb.h>

class TUrlExtCrawlData;
class THostHistoryExtData;

#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#pragma pack(push, 4)

struct TSitemapStatRecOld
{
    ui64    UrlId;
    ui32    HostId;
    ui32    HttpCode;
    TDataworkDate Added;
    TDataworkDate ModTime;
    TDataworkDate LastAccess;
    TDataworkDate LastSuccessAccess;
    TDataworkDate LastModified;
    ui32    DownloadTryCount;
    ui32    ParseTryCount;
    ui32    UrlsCount;
    ui16    Flags;    // Types of content: video, image, mobile etc. May be or-ed.
    ui8     Format;   // Text/Xml
    ui8     Type;     // Sitemap/sitemapindex
    ui8     Status;
    ui8 Dummy1; ui8 Dummy2; ui8 Dummy3; // Needed due to the 32-bit field alignment
    ui32    TruthfulCrawl;
    ui32    BaldLieCrawl;

    url_t   Name;

    enum {
        NEW,
        INDEXED,
        FILTERED
    };

    enum {
        VIDEO = 1,
        IMAGE = 2,
        MOBILE = 4
    };

    enum {
        TEXT = 1,
        XML = 2,
        RSS = 3
    };

    enum {
        SITEMAP = 1,
        SITEMAPINDEX = 2
    };

    static const ui32 RecordSig = 0x42346454;

    size_t SizeOf() const {
        return strchr(Name, 0) + 1 - (char*)(void*)this;
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

};


//
// New new sitemap record
struct TSitemapStatRecNew
{
    ui64    UrlId;
    ui32    HostId;
    ui32    HttpCode;
    TDataworkDate Added;
    TDataworkDate ModTime;
    TDataworkDate LastAccess;
    TDataworkDate LastSuccessAccess;
    TDataworkDate LastModified;
    ui32    DownloadTryCount;
    ui32    ParseTryCount;
    ui32    UrlsCount;
    ui16    Flags;    // Types of content: video, image, mobile etc. May be or-ed.
    ui8     Format;   // Text/Xml
    ui8     Type;     // Sitemap/sitemapindex
    ui8     Status;
    ui8 Dummy1; ui8 Dummy2; ui8 Dummy3; // Needed due to the 32-bit field alignment
    ui32    TruthfulCrawl;
    ui32    BaldLieCrawl;
    ui32    ChangeFrequency;
    ui32    AverageNumOfChangedUrls;
    ui32    NumOfCrawlsForAverageChangedUrls;

    url_t   Name;

    enum {
        NEW,
        INDEXED,
        FILTERED
    };

    enum {
        VIDEO = 1,
        IMAGE = 2,
        MOBILE = 4
    };

    enum {
        TEXT = 1,
        XML = 2,
        RSS = 3
    };

    enum {
        SITEMAP = 1,
        SITEMAPINDEX = 2
    };

    static const ui32 RecordSig = 0x42346747;

    static size_t OffsetOfName() {
        static TSitemapStatRecNew rec;
        return rec.OffsetOfNameImpl();
    }

    size_t SizeOf() const {
        return strchr(Name, 0) + 1 - (char*)(void*)this;
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TSitemapStatRecNew *a, const TSitemapStatRecNew *b) {
        return CompareUids(a, b);
    }

    static int ByUidDescLastAccess(const TSitemapStatRecNew* a, const TSitemapStatRecNew* b) {
        int res = CompareUids(a, b);
        return res ? res : ::compare(b->LastAccess, a->LastAccess);
    }

private:
    size_t OffsetOfNameImpl() const {
        return offsetofthis(Name);
    }
};

MAKESORTERTMPL(TSitemapStatRecNew, ByUid);
MAKESORTERTMPL(TSitemapStatRecNew, ByUidDescLastAccess);

//
// First 100 sitemap errors and first 100 warnings
struct TSitemapErrorsRecOld {

    // Unstrutctured field type. We need special type for datawork
    struct Stream {
        ui32 Size;
        char Data[1 << 20];
    };

    // One error record
    struct Record {
        ui8 Type;    // Types listed in sitemapprocessor/pareser.h
        ui16 Line;
        ui16 Column;
        url_t Text;  // in some cases contain url, but not always

        size_t SizeOf() const {
            return strchr(Text, 0) + 1 - (char*)(void*)this;
        }
    };

    ui64            UrlId;
    ui32            HostId;
    Stream          Errors;

    static const ui32 RecordSig = 0x12347112;

    size_t SizeOf() const {
        return offsetofthis(Errors) + Errors.Size;
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TSitemapErrorsRecOld *a, const TSitemapErrorsRecOld *b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TSitemapErrorsRecOld, ByUid);

//
// First 100 sitemap errors and first 100 warnings
struct TSitemapErrorsRec {

    // Unstrutctured field type. We need special type for datawork
    struct Stream {
        ui32 Size;
        char Data[1 << 20];
    };

    // One error record
    struct Record {
        ui32 Type;    // Types listed in sitemapprocessor/pareser.h
        ui16 Line;
        ui16 Column;
        url_t Text;  // in some cases contain url, but not always

        size_t SizeOf() const {
            return strchr(Text, 0) + 1 - (char*)(void*)this;
        }
    };

    ui64            UrlId;
    ui32            HostId;
    Stream          Errors;

    static const ui32 RecordSig = 0x12347124;

    size_t SizeOf() const {
        return offsetofthis(Errors) + Errors.Size;
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TSitemapErrorsRec *a, const TSitemapErrorsRec *b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TSitemapErrorsRec, ByUid);

//
// Urls from sitemap
// 50 000 maximum (50 000 * 12 = 600 000 = 600 Kb)
struct TSitemapUrlsRec {

    // Unstrutctured field type. We need special type for datawork
    struct Stream {
        ui32 Size;
        char Data[1 << 20];
    };

    // One url record
    struct Record {
        ui64 UrlId;
        ui32 HostId;

        size_t SizeOf() const {
            return sizeof(*this);
        }

        bool operator < (const Record& r) const {
            return (CompareUids(this, &r) < 0);
        }

        operator urlid_t() const {
            return urlid_t(HostId, UrlId);
        }
    };

    ui64            UrlId;
    ui32            HostId;
    Stream          Urls;

    static const ui32 RecordSig = 0x12347114;

    size_t SizeOf() const {
        return offsetofthis(Urls) + Urls.Size;
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TSitemapUrlsRec *a, const TSitemapUrlsRec *b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TSitemapUrlsRec, ByUid);

//
// From webmasters
struct TWebmasterSitemapRec
{
    ui32   HostId;       // sitemap host
    ui32   OwnerHostId;  // owner host
    TDataworkDate Added;
    url_t  Name;         // sitemap path

    static const ui32 RecordSig = 0x12347118;

    static size_t OffsetOfName() {
        static TWebmasterSitemapRec rec;
        return rec.OffsetOfNameImpl();
    }

    size_t SizeOf() const {
        return strchr(Name, 0) + 1 - (char*)(void*)this;
    }

private:
    size_t OffsetOfNameImpl() const {
        return offsetofthis(Name);
    }
};

struct TAllowedSitemapRec
{
    ui64   UrlId;
    ui32   HostId;
    ui32   ValidatedHostId;
    url_t  Name;         // sitemap path

    static const ui32 RecordSig = 0x12347120;

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByUidValidatedHostId(const TAllowedSitemapRec *a, const TAllowedSitemapRec *b) {
        int uidsCompare = CompareUids(a, b);
        if (uidsCompare == 0) return a->ValidatedHostId < b->ValidatedHostId ? -1 : 0;
        return uidsCompare;
    }
};

MAKESORTERTMPL(TAllowedSitemapRec, ByUidValidatedHostId);

struct TWebmasterAllowedSitemapRec
{
    ui32   ValidatedHostId;
    ui32   HostId;
    ui64   UrlId;

    static const ui32 RecordSig = 0x12348120;

    operator hostid_t() const {
        return ValidatedHostId;
    }

    static int ByValidatedHostId(const TWebmasterAllowedSitemapRec *a, const TWebmasterAllowedSitemapRec *b) {
        return ::compare(a->ValidatedHostId, b->ValidatedHostId);
    }
};

MAKESORTERTMPL(TWebmasterAllowedSitemapRec, ByValidatedHostId);

struct TSitemapSourceRec
{
    ui64   UrlId;
    ui32   HostId;
    ui32   SitemapSources; // flags (see below)

    // Confirmation types
    enum  {
        ROBOTS = 1,
        SITEMAPINDEX = 2,
        WEBMASTER = 4,
        REDIRECT = 8
    };

    static const ui32 RecordSig = 0x12347122;

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TSitemapSourceRec *a, const TSitemapSourceRec *b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TSitemapSourceRec, ByUid);

struct TUrlFromSitemapInfo
{
    ui64   UrlId;
    ui32   HostId;
    double Priority;
    time_t LastModified;
    ui32   ChangeFrequency;

    static const ui32 RecordSig = 0x12347121;

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TUrlFromSitemapInfo *a, const TUrlFromSitemapInfo *b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TUrlFromSitemapInfo, ByUid);

struct TLink
{
    ui64    SrcUrlId;
    ui64    DstUrlId;
    ui32    SrcHostId;
    ui32    DstHostId;

    static const ui32 RecordSig = 0x12346301;

    operator urlid_t() const {
        return urlid_t(SrcHostId, SrcUrlId);
    }

    void SetSrcUid(const urlid_t& from) {
        SrcUrlId = from.UrlId;
        SrcHostId = from.HostId;
    }

    void SetDstUid(const urlid_t& from) {
        DstUrlId = from.UrlId;
        DstHostId = from.HostId;
    }

    static int BySrcUid(const TLink* a, const TLink* b) {
        int ret = ::compare(a->SrcHostId, b->SrcHostId);
        return ret ? ret : ::compare(a->SrcUrlId, b->SrcUrlId);
    }
    static int BySrcUid(const TLink* a, const TUrlBase* b) {
        int ret = ::compare(a->SrcHostId, b->HostId);
        return ret ? ret : ::compare(a->SrcUrlId, b->UrlId);
    }

    static int ByDstUid(const TLink* a, const TLink* b) {
        int ret = ::compare(a->DstHostId, b->DstHostId);
        return ret ? ret : ::compare(a->DstUrlId, b->DstUrlId);
    }
};

MAKESORTERTMPL(TLink, BySrcUid);
MAKESORTERTMPL(TLink, ByDstUid);

struct TLinkDelay : public TLink {
    enum {
        OnSearch ,
        Prolong ,
        InBase
    };

    ui64    CaseSensitiveSrcUrlId;
    ui16    State;//use enum above
    ui16    ForeignWalrus;//bool flag
    time_t  Time;
    i32     LastAccess;
    ui32    Size;
    ui32    Flags;
    ui32    HttpCode;
    url_t   SrcUrl;
    url_t   DstUrl;

    static size_t OffsetOfSrcUrl() {
        static TLinkDelay rec;
        return rec.OffsetOfSrcUrlImpl();
    }

    static const ui32 RecordSig = 0x12348596;
    static const time_t LiveOnSearch = 16 * 24 * 60 * 60;// 16 days
    static const time_t LiveProlong = 28 * 24 * 60 * 60;// four weeks

private:
    size_t OffsetOfSrcUrlImpl() const {
        return offsetofthis(SrcUrl);
    }

};

MAKESORTERTMPL(TLinkDelay, BySrcUid);
MAKESORTERTMPL(TLinkDelay, ByDstUid);

struct TLinkSO : public TLink
{
    static const ui32 RecordSig = 0x12346302;
    ui32 SrcOwnerId;

    static int ByDstUid(const TLinkSO* a, const TLinkSO* b) {
        int ret = ::compare(a->DstHostId, b->DstHostId);
        ret = ret ? ret : ::compare(a->DstUrlId, b->DstUrlId);
        ret = ret ? ret : ::compare(a->SrcOwnerId, b->SrcOwnerId);
        return ret ? ret : ::compare(a->SrcHostId, b->SrcHostId);
    }
};

MAKESORTERTMPL(TLinkSO, ByDstUid);
MAKESORTERTMPL(TLinkSO, BySrcUid);

struct TNamedLink : public TLink {

    static const ui32 RecordSig = 0x12347400;

    redir_t SrcDstNames;

    static size_t OffsetOfNames() {
        static TNamedLink link;
        return link.OffsetOfNamesImpl();
    }
    size_t SizeOf() const {
        size_t srclen = strlen(SrcDstNames);
        size_t dstlen = strlen(SrcDstNames + srclen + 1);
        return OffsetOfNames() + srclen + dstlen + 2;
    }

private:
    size_t OffsetOfNamesImpl() const {
        return offsetofthis(SrcDstNames);
    }
};

MAKESORTERTMPL(TNamedLink, BySrcUid);
MAKESORTERTMPL(TNamedLink, ByDstUid);

struct TRefDocUidRec: public TDocUidRec {
    ui32 Deleted;
    ui32 Reason;

    static const ui32 RecordSig = 0x12347399;

    static int ByDocIdDeleted(const TRefDocUidRec *a, const TRefDocUidRec *b) {
        int ret = ::compare(a->DocId, b->DocId);
        return ret ? ret : ::compare(a->Deleted, b->Deleted);
    }

    static int ByUidDeleted(const TRefDocUidRec *a, const TRefDocUidRec *b) {
        int ret = CompareUids(a, b);
        return ret ? ret : ::compare(a->Deleted, b->Deleted);
    }
};

MAKESORTERTMPL(TRefDocUidRec, ByUid);
MAKESORTERTMPL(TRefDocUidRec, ByDocUid);
MAKESORTERTMPL(TRefDocUidRec, ByHostDoc);
MAKESORTERTMPL(TRefDocUidRec, ByUidDeleted);
MAKESORTERTMPL(TRefDocUidRec, ByDocIdDeleted);

struct TIndCrcRec {
    ui64    Crc;
    ui32    HostId;
    ui32    UrlLength;
    ui32    RecNum;

    static const ui32 RecordSig = 0x12347304;

    static int ByHostCrcLength(const TIndCrcRec *a, const TIndCrcRec *b) {
        int ret = ::compare(a->HostId, b->HostId);
        ret = ret ? ret : ::compare(a->Crc, b->Crc);
        return ret ? ret : ::compare(a->UrlLength, b->UrlLength);
    }

    static int ByCrcHostLength(const TIndCrcRec *a, const TIndCrcRec *b) {
        int ret = ::compare(a->Crc, b->Crc);
        ret = ret ? ret : ::compare(a->HostId, b->HostId);
        return ret ? ret : ::compare(a->UrlLength, b->UrlLength);
    }
};

MAKESORTERTMPL(TIndCrcRec, ByHostCrcLength);
MAKESORTERTMPL(TIndCrcRec, ByCrcHostLength);

struct TIndCrcRecUidOld : public TIndCrcRec
{
    ui64 UrlId;
    ui64 RelUid;

    static const ui32 RecordSig = 0x1234730F;

    static int ByUid(const TIndCrcRecUidOld *a, const TIndCrcRecUidOld *b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TIndCrcRecUidOld, ByUid);
MAKESORTERTMPL(TIndCrcRecUidOld, ByHostCrcLength);
MAKESORTERTMPL(TIndCrcRecUidOld, ByCrcHostLength);

struct TIndCrcRecUid : public TIndCrcRec
{
    ui64 UrlId;
    ui64 RelUid;
    ui32 SimhashVersion;
    ui64 Simhash;
    ui32 SimhashDocLength;
    ui32 SimhashTitleHash;

    static const ui32 RecordSig = 0x1504730F;

    static int ByUid(const TIndCrcRecUid *a, const TIndCrcRecUid *b) {
        return CompareUids(a, b);
    }

    static int ByRecNum(const TIndCrcRecUid *a, const TIndCrcRecUid *b) {
        return ::compare(a->RecNum, b->RecNum);
    }
};

MAKESORTERTMPL(TIndCrcRecUid, ByUid);
MAKESORTERTMPL(TIndCrcRecUid, ByHostCrcLength);
MAKESORTERTMPL(TIndCrcRecUid, ByCrcHostLength);
MAKESORTERTMPL(TIndCrcRecUid, ByRecNum);

struct TIndCrcRecUidFileNo : public TIndCrcRecUid {
    ui32 FileNo;

    static const ui32 RecordSig = 0x1504730E;
};

MAKESORTERTMPL(TIndCrcRecUidFileNo, ByUid);
MAKESORTERTMPL(TIndCrcRecUidFileNo, ByHostCrcLength);
MAKESORTERTMPL(TIndCrcRecUidFileNo, ByCrcHostLength);
MAKESORTERTMPL(TIndCrcRecUidFileNo, ByRecNum);

struct TDocNumRec {
    ui32    DocId;
    ui32    RecNum;
    ui32    Flags;

    static const ui32 RecordSig = 0x12347305;

    static int ByDoc(const TDocNumRec *a, const TDocNumRec *b) {
        return ::compare(a->DocId, b->DocId);
    }

    static int ByNewSigRecNum(const TDocNumRec *a, const TDocNumRec *b) {
        int ret = ::compare(b->Flags & UrlFlags::NEWSIG, a->Flags & UrlFlags::NEWSIG);
        return ret ? ret : ::compare(a->RecNum, b->RecNum);
    }

    static int ByRecNum(const TDocNumRec *a, const TDocNumRec *b) {
        return ::compare(a->RecNum, b->RecNum);
    }
};

MAKESORTERTMPL(TDocNumRec, ByDoc);
MAKESORTERTMPL(TDocNumRec, ByNewSigRecNum);
MAKESORTERTMPL(TDocNumRec, ByRecNum);

struct TDocNumHostRec : public TDocNumRec {
    ui32 HostId;
    static const ui32 RecordSig = 0x12347308;
};

struct TDocNumFileNoRec : public TDocNumRec {
    ui32 FileNo;
    ui64 Crc;
    static const ui32 RecordSig = 0x12347307;
};

MAKESORTERTMPL(TDocNumFileNoRec, ByDoc);
MAKESORTERTMPL(TDocNumFileNoRec, ByNewSigRecNum);
MAKESORTERTMPL(TDocNumFileNoRec, ByRecNum);

struct TSemidupRec {
    ui64    UrlId;
    ui64    Crc;
    ui32    HostId;
    ui32    DocId;
    ui32    GroupId;
    ui32    UrlLength;

    static const ui32 RecordSig = 0x12347306;

    static int ByUid(const TSemidupRec *a, const TSemidupRec *b) {
        return CompareUids(a, b);
    }

    static int ByHostCrcUrlId(const TSemidupRec *a, const TSemidupRec *b) {
        int ret = ::compare(a->HostId, b->HostId);
        if (!ret)
            ret = ::compare(a->Crc, b->Crc);
        return ret ? ret : ::compare(a->UrlId, b->UrlId);
    }

    static int ForSemidups(const TSemidupRec *a, const TSemidupRec *b) {
        int ret = ::compare(a->GroupId, b->GroupId);
        ret = ret ? ret : ::compare(a->HostId, b->HostId);
        return ret ? ret : a->UrlLength - b->UrlLength;
    }
};

MAKESORTERTMPL(TSemidupRec, ByUid);
MAKESORTERTMPL(TSemidupRec, ByHostCrcUrlId);
MAKESORTERTMPL(TSemidupRec, ForSemidups);

struct TNewUrlHeader {
    TDataworkDate     AddTime;
    ui32    Flags;
    ui32    Hops;
};

struct TNewUrlRec : public urlid_t, public TNewUrlHeader {
    url_t   Name;

    typedef NRobotScheeme::TUrlAttrs TExtInfo;
    static const ui32 RecordSig = 0x12347337;

    static size_t OffsetOfName() {
        static TNewUrlRec rec;
        return rec.OffsetOfNameImpl();
    }

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByUidLength(const TNewUrlRec *a, const TNewUrlRec *b) {
        int ret = CompareUids(a, b);
        if (ret)
            return ret;
        return (int)strlen(a->Name) - (int)strlen(b->Name);
    }

    static int ByUidHops(const TNewUrlRec *a, const TNewUrlRec *b) {
        int ret = CompareUids(a, b);
        if (ret)
            return ret;
        return ::compare(a->Hops, b->Hops);
    }

    static int ByUidHopsLength(const TNewUrlRec *a, const TNewUrlRec *b) {
        int ret = ByUidHops(a, b);
        if (ret)
            return ret;
        return (int)strlen(a->Name) - (int)strlen(b->Name);
    }

    static int ByUidFlagLength(const TNewUrlRec *a, const TNewUrlRec *b) {
        int ret = CompareUids(a, b);
        if (ret)
            return ret;
        ret = ::compare(b->Flags & UrlFlags::FORCEREPLACE, a->Flags & UrlFlags::FORCEREPLACE);
        if (ret)
            return ret;
        return (int)strlen(a->Name) - (int)strlen(b->Name);
    }

private:
    size_t OffsetOfNameImpl() const {
        return offsetofthis(Name);
    }

};

MAKESORTERTMPL(TNewUrlRec, ByUid);
MAKESORTERTMPL(TNewUrlRec, ByUidLength);
MAKESORTERTMPL(TNewUrlRec, ByUidHops);
MAKESORTERTMPL(TNewUrlRec, ByUidHopsLength);
MAKESORTERTMPL(TNewUrlRec, ByUidFlagLength);


struct TNewUrlLenRec : public urlid_t, public TNewUrlHeader {
    ui16 NameLen;
    url_t   Name;

    static const ui32 RecordSig = 0x12347338;

    static size_t OffsetOfName() {
        return offsetof(TNewUrlLenRec, Name);
    }

    size_t SizeOf() const {
        return OffsetOfName() + NameLen + 1;
    }

    static int ByUidHops(const TNewUrlLenRec *a, const TNewUrlLenRec *b) {
        int ret = CompareUids(a, b);
        if (ret)
            return ret;
        return ::compare(a->Hops, b->Hops);
    }

    static int ByUidHopsLength(const TNewUrlLenRec *a, const TNewUrlLenRec *b) {
        int ret = ByUidHops(a, b);
        if (ret)
            return ret;
        return (int)a->NameLen - (int)b->NameLen;
    }

    void CopyTo(TNewUrlRec* newUrl) const {
        memcpy(newUrl, this, TNewUrlRec::OffsetOfName());
        memcpy(newUrl->Name, Name, NameLen + 1);
    }
};

MAKESORTERTMPL(TNewUrlLenRec, ByUidHops);
MAKESORTERTMPL(TNewUrlLenRec, ByUidHopsLength);

struct TForeignUrlRec {
    ui64 UrlId;
    ui64 Langs;
    ui32 PathLength;
    ui32 HostLength;
    ui32 Weight;
    url_t Name;

    static const ui32 RecordSig = 0x12347339;

    static size_t OffsetOfName() {
        static TForeignUrlRec rec;
        return rec.OffsetOfNameImpl();
    }

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

private:
    size_t OffsetOfNameImpl() const {
        return offsetofthis(Name);
    }
};

struct TUpdUrlHeader {
    i32         LastAccess;
    ui32        Size;
    ui32        Flags;
    ui32        HttpCode;
    i32         HttpModTime;
    ui16        MimeType;
    i8          Encoding;
    ui8         Language;
};

struct TUpdUrlRec : public urlid_t, public TUpdUrlHeader {
    ui32    Ctx; //return policyId
    url_t   Name;

    typedef NRobotScheeme::TUrlAttrs TExtInfo;
    static const ui32 RecordSig = 0x12347518;

    static size_t OffsetOfName() {
        static TUpdUrlRec rec;
        return rec.OffsetOfNameImpl();
    }

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByUidTime(const TUpdUrlRec *a, const TUpdUrlRec *b) {
        int ret = CompareUids(a, b);
        return ret ? ret : ::compare(a->LastAccess, b->LastAccess);
    }

    static int ByUidLengthTime(const TUpdUrlRec *a, const TUpdUrlRec *b) {
        int ret = CompareUids(a, b);
        if (ret)
            return ret;
        ret = (int)strlen(a->Name) - (int)strlen(b->Name);
        return ret ? ret : ::compare(b->LastAccess, a->LastAccess);
    }

    static int ByUidLengthAcTimeHtTime(const TUpdUrlRec *a, const TUpdUrlRec *b) {
        int ret = ByUidLengthTime(a, b);
        return ret ? ret : ::compare(b->HttpModTime, a->HttpModTime);
    }

private:
    size_t OffsetOfNameImpl() const {
        return offsetofthis(Name);
    }
};

MAKESORTERTMPL(TUpdUrlRec, ByUid);
MAKESORTERTMPL(TUpdUrlRec, ByUidTime);
MAKESORTERTMPL(TUpdUrlRec, ByUidLengthTime);
MAKESORTERTMPL(TUpdUrlRec, ByUidLengthAcTimeHtTime);

struct TUpdSitemapUrlRec : public urlid_t, public TUpdUrlHeader {
    url_t   Name;

    static const ui32 RecordSig = 0x12347319;

    static size_t OffsetOfName() {
        static TUpdSitemapUrlRec rec;
        return rec.OffsetOfNameImpl();
    }

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByUidTime(const TUpdSitemapUrlRec *a, const TUpdSitemapUrlRec *b) {
        int ret = CompareUids(a, b);
        return ret ? ret : ::compare(a->LastAccess, b->LastAccess);
    }

    static int ByUidLengthTime(const TUpdSitemapUrlRec *a, const TUpdSitemapUrlRec *b) {
        int ret = CompareUids(a, b);
        if (ret)
            return ret;
        ret = (int)strlen(a->Name) - (int)strlen(b->Name);
        return ret ? ret : ::compare(b->LastAccess, a->LastAccess);
    }

private:
    size_t OffsetOfNameImpl() const {
        return offsetofthis(Name);
    }
};

MAKESORTERTMPL(TUpdSitemapUrlRec, ByUid);
MAKESORTERTMPL(TUpdSitemapUrlRec, ByUidTime);
MAKESORTERTMPL(TUpdSitemapUrlRec, ByUidLengthTime);


struct TIndUrlHeader {
    ui64        Crc;
    TDataworkDate         HttpModTime;
    TDataworkDate         LastAccess;
    ui32        Size;
    ui32        Flags;
    ui32        HttpCode;
    ui8         MimeType;
    i8          Encoding;
    ui8         Language;
    ui8         Hops;
    TShingle    Shingle;
};

struct TIndUrlDoc : public TIndUrlHeader {
    packed_stream_t DataStream;
};

struct TIndUrlHeaderOld {
    ui64        Crc;
    TDataworkDate         HttpModTime;
    TDataworkDate         LastAccess;
    ui32        Size;
    ui32        Flags;
    ui32        HttpCode;
    ui8         MimeType;
    i8          Encoding;
    ui8         Language;
    ui8         Hops;
};


struct TDocRec : public urlid_t, public TIndUrlDoc {

    static const ui32 RecordSig = 0x12347321;

    size_t SizeOf() const {
        return offsetofthis(DataStream) + DataStream.SizeOf();
    }

    static int ByUidTime(const TDocRec *a, const TDocRec *b) {
        int ret = CompareUids(a, b);
        return ret ? ret : ::compare(a->LastAccess, b->LastAccess);
    }
};

struct TDocRecOld : public urlid_t, public TIndUrlHeaderOld {
    packed_stream_t DataStream;

    static const ui32 RecordSig = 0x12347311;

    size_t SizeOf() const {
        return offsetofthis(DataStream) + DataStream.SizeOf();
    }

    static int ByUidTime(const TDocRecOld *a, const TDocRecOld *b) {
        int ret = CompareUids(a, b);
        return ret ? ret : ::compare(a->LastAccess, b->LastAccess);
    }
};

MAKESORTERTMPL(TDocRec, ByUid);
MAKESORTERTMPL(TDocRec, ByUidTime);

MAKESORTERTMPL(TDocRecOld, ByUid);
MAKESORTERTMPL(TDocRecOld, ByUidTime);

struct TSitemapRec : public urlid_t, public TIndUrlHeader {
    packed_stream_t DataStream;

    static const ui32 RecordSig = 0x12347325;

    size_t SizeOf() const {
        return offsetofthis(DataStream) + DataStream.SizeOf();
    }

    static int ByUidTime(const TSitemapRec *a, const TSitemapRec *b) {
        int ret = CompareUids(a, b);
        return ret ? ret : ::compare(a->LastAccess, b->LastAccess);
    }
};

MAKESORTERTMPL(TSitemapRec, ByUid);
MAKESORTERTMPL(TSitemapRec, ByUidTime);

struct TIndUrlRec : public urlid_t, public TIndUrlHeader {
    ui32        DocId;
    ui32        RecNum;
    ui32        Ctx;
    url_t       Name;

    typedef NRobotScheeme::TUrlAttrs TExtInfo;
    static const ui32 RecordSig = 0x1234732C;

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByUidTime(const TIndUrlRec *a, const TIndUrlRec *b) {
        int ret = CompareUids(a, b);
        return ret ? ret : ::compare(a->LastAccess, b->LastAccess);
    }

    static int ByUidLengthTime(const TIndUrlRec *a, const TIndUrlRec *b) {
        int ret = CompareUids(a, b);
        if (ret)
            return ret;
        ret = (int)strlen(a->Name) - (int)strlen(b->Name);
        return ret ? ret : ::compare(b->LastAccess, a->LastAccess);
    }

    static int ByRecNum(const TIndUrlRec *a, const TIndUrlRec *b) {
        return ::compare(a->RecNum, b->RecNum);
    }

    static size_t OffsetOfName() {
        static TIndUrlRec rec;
        return rec.OffsetOfNameImpl();
    }

private:
    size_t OffsetOfNameImpl() const {
        return offsetofthis(Name);
    }
};

struct TIndUrlRecOld2 : public urlid_t, public TIndUrlHeader {
    ui32        DocId;
    url_t       Name;

    static const ui32 RecordSig = 0x1234732A;

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByUidTime(const TIndUrlRecOld2 *a, const TIndUrlRecOld2 *b) {
        int ret = CompareUids(a, b);
        return ret ? ret : ::compare(a->LastAccess, b->LastAccess);
    }

    static int ByUidLengthTime(const TIndUrlRecOld2 *a, const TIndUrlRecOld2 *b) {
        int ret = CompareUids(a, b);
        if (ret)
            return ret;
        ret = (int)strlen(a->Name) - (int)strlen(b->Name);
        return ret ? ret : ::compare(b->LastAccess, a->LastAccess);
    }
};

struct TIndUrlRecOld : public urlid_t, public TIndUrlHeaderOld {
    ui32        DocId;
    url_t       Name;

    static const ui32 RecordSig = 0x1234730A;

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByUidTime(const TIndUrlRecOld *a, const TIndUrlRecOld *b) {
        int ret = CompareUids(a, b);
        return ret ? ret : ::compare(a->LastAccess, b->LastAccess);
    }

    static int ByUidLengthTime(const TIndUrlRecOld *a, const TIndUrlRecOld *b) {
        int ret = CompareUids(a, b);
        if (ret)
            return ret;
        ret = (int)strlen(a->Name) - (int)strlen(b->Name);
        return ret ? ret : ::compare(a->LastAccess, b->LastAccess);
    }
};

MAKESORTERTMPL(TIndUrlRec, ByUid);
MAKESORTERTMPL(TIndUrlRec, ByRecNum);
MAKESORTERTMPL(TIndUrlRec, ByUidTime);
MAKESORTERTMPL(TIndUrlRec, ByUidLengthTime);

MAKESORTERTMPL(TIndUrlRecOld2, ByUid);
MAKESORTERTMPL(TIndUrlRecOld2, ByUidTime);
MAKESORTERTMPL(TIndUrlRecOld2, ByUidLengthTime);

MAKESORTERTMPL(TIndUrlRecOld, ByUid);
MAKESORTERTMPL(TIndUrlRecOld, ByUidTime);
MAKESORTERTMPL(TIndUrlRecOld, ByUidLengthTime);

#ifdef COMPAT_ROBOT_STABLE
#   define TIndUrlRec TIndUrlRecOld2
#endif

struct TUrlCrawlHeader : public urlid_t {
    ui32 Size;
};

struct TUrlCrawlHeaderRec : public TUrlCrawlHeader {
    ui32 RecNum;
};

struct TIndUrlCrawlDataTransit : public TUrlCrawlHeaderRec {
    urlextradata_t Data;

    static const ui32 RecordSig = 0x22332222;

    inline size_t SizeOf() const {
        return SizeOfFixed() + Size;
    }
    template<typename T>
    inline void AssignFixed(T const& uid, ui32 const size, ui32 const recNum) {
        SetUid(this, &uid);
        Size = size;
        RecNum = recNum;
    }
    inline void AssignData(void const* data, size_t const size) {
        Size = (ui32)size;
        memcpy(Data, data, size);
    }

    static int ByUid(const TIndUrlCrawlDataTransit *a, const TIndUrlCrawlDataTransit *b) {
        return CompareUids(a, b);
    }
    static size_t SizeOfFixed() { return offsetof(TIndUrlCrawlDataTransit, Data); }
    static size_t SizeToStore(size_t const varDataSize) { return offsetof(TIndUrlCrawlDataTransit, Data) + varDataSize; }
    static size_t GetMaxVarDataCapacity() { return sizeof(urlextradata_t); }
};

MAKESORTERTMPL(TIndUrlCrawlDataTransit, ByUid);

struct TIndUrlCrawlData : public TUrlCrawlHeader {
    urlextradata_t Data;

    static const ui32 RecordSig = 0x22332224;

    inline size_t SizeOf() const {
        return SizeOfFixed() + Size;
    }
    template<typename T>
    inline void AssignFixed(T const& uid, size_t const size) {
        SetUid(this, &uid);
        Size = (ui32)size;
    }
    inline void AssignData(void const* data, size_t const size) {
        Size = (ui32)size;
        memcpy(Data, data, size);
    }

    static int ByUid(const TIndUrlCrawlData *a, const TIndUrlCrawlData *b) {
        return CompareUids(a, b);
    }
    static size_t SizeOfFixed() { return offsetof(TIndUrlCrawlData, Data); }
    static size_t SizeToStore(size_t const varDataSize) { return offsetof(TIndUrlCrawlData, Data) + varDataSize; }
    static size_t GetMaxVarDataCapacity() { return sizeof(urlextradata_t); }
};

MAKESORTERTMPL(TIndUrlCrawlData, ByUid);

struct TUrlDictRec {
    ui32        HostId;
    ui32        Docs;
    ui32        Dict;
    ui32        Antiq;
    ui32        New;
    url_t       Dir;

    static const ui32 RecordSig = 0x1234730B;
    size_t SizeOf() const {
        return offsetofthis(Dir) + strlen(Dir) + 1;
    }
};

struct THostDirPlanRec {
    ui32        HostId;
    ui32        Plan;
    ui32        NewQ;
    ui32        Left;
    ui32        Total;
    url_t       Dir;

    static const ui32 RecordSig = 0x1234730C;
    size_t SizeOf() const {
        return offsetofthis(Dir) + strlen(Dir) + 1;
    }
};


struct TPortionRec {
    ui32    Weight;
    ui64    Langs;
    url_t   Name;

    static const ui32 RecordSig = 0x1234730D;

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByName(const TPortionRec *a, const TPortionRec *b) {
        return stricmp(a->Name, b->Name);
    }
};

MAKESORTERTMPL(TPortionRec, ByName);

struct TPortionWithUidRec {
    ui64    UrlId;
    ui64    Langs;
    ui32    HostId;
    ui32    Weight;
    url_t   Name;

    static const ui32 RecordSig = 0x1234732D;

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByUid(const TPortionWithUidRec* lhs, const TPortionWithUidRec* rhs) {
        return CompareUids(lhs, rhs);
    }

};

MAKESORTERTMPL(TPortionWithUidRec, ByUid);

struct TUrlWeightRec : public urlid_t {
    ui32    Weight;
    ui64    Langs;

    static const ui32 RecordSig = 0x1234730E;

    size_t SizeOf() const {
        return sizeof(TUrlWeightRec);
    }

    static int ByWeight(const TUrlWeightRec* a, const TUrlWeightRec* b) {
        return ::compare(a->Weight, b->Weight);
    }
};

MAKESORTERTMPL(TUrlWeightRec, ByUid);
MAKESORTERTMPL(TUrlWeightRec, ByWeight);

struct THostUrlIdRec
{
    ui64    UrlId;
    dbscheeme::host_t  Hostname;

    static const ui32 RecordSig = 0x12347310;

    size_t SizeOf() const
    {
        return Hostname - (char*)this + strlen(Hostname) + 1;
    }

    static int ByHostUrlId(const THostUrlIdRec* u1, const THostUrlIdRec* u2)
    {
        int cmp;
        return (cmp = strcmp(u1->Hostname, u2->Hostname)) ? cmp : ::compare(u1->UrlId, u2->UrlId);
    }
};

MAKESORTERTMPL(THostUrlIdRec, ByHostUrlId);

template <typename TBuffer>
struct TDocBlobRecBase {
    ui32 DocId;
    TBuffer Buffer;

    // Got internal compiler error here
    //static_assert(offsetof(TDocBlobRecBase, Buffer) == 4, "expect offsetof(TDocBlobRecBase, Buffer) == 4");
    //static_assert(offsetof(TDynamicCharBuffer, Data) == 4, "expect offsetof(TDynamicCharBuffer, Data) == 4");

    static size_t SizeOfForDataSize(size_t dataSize) {
        static TDocBlobRecBase<TBuffer> rec;
        return rec.OffsetOfBufferImpl() + TDynamicCharBuffer::OffsetOfData() + dataSize;
    }

    size_t SizeOf() const {
        return SizeOfForDataSize(Buffer.Size);
    }

    static int ByDocId(const TDocBlobRecBase* rec1, const TDocBlobRecBase* rec2)
    {
        return ::compare(rec1->DocId, rec2->DocId);
    }

private:
    size_t OffsetOfBufferImpl() const {
        return offsetofthis(Buffer);
    }
};

struct TDocBinaryBlobRec: public TDocBlobRecBase<TDynamicCharBuffer> {
    static const ui32 RecordSig = 0x12347313;
};

MAKESORTERTMPL(TDocBinaryBlobRec, ByDocId);

#define MAKE_4C(a, b, c, d) a * (1 << 24) + b * (1 << 16) + c * (1 << 8) + d

struct TDocArchheadersRec: public TDocBlobRecBase<TStringStringMapBuffer> {
    static const ui32 RecordSig = MAKE_4C('D', 'A', 'h', 'd');
};

MAKESORTERTMPL(TDocArchheadersRec, ByDocId);

struct TFilterUidRec : public urlid_t {
    ui32 Code;
    ui64 GroupId;

    static const ui32 RecordSig = 0x1234731F;
};

MAKESORTERTMPL(TFilterUidRec, ByUid);

struct TUpdUrlDelayUidRec : public urlid_t {
    ui32 Time;

    static const ui32 RecordSig = 0x1237631F;
};

MAKESORTERTMPL(TUpdUrlDelayUidRec, ByUid);

struct TRobotZoneUidRec : public urlid_t {
    ui32 Zone;

    static const ui32 RecordSig = 0x1234731C;
};

MAKESORTERTMPL(TRobotZoneUidRec, ByUid);

struct TUrlSuperiority {
    ui64 UrlId;
    ui32 HostId;
    ui32 DocId;
    i32  Superiority;

    static const ui32 RecordSig = 0x15000001;

    operator docid_t() const {
        return DocId;
    }

    static int ByDocSup(const TUrlSuperiority *a, const TUrlSuperiority *b)
    {
         int ret = ::compare(a->DocId, b->DocId);
         if (ret)
             return ret;
         ret = ::compare(b->Superiority, a->Superiority); // bigger Superiority first
         return ret ? ret : CompareUids(a, b);
    }
};

MAKESORTERTMPL(TUrlSuperiority, ByDocSup);

struct time_hostid {
    ui32 HostId;
    ui32 CrawlTime;

    static const ui32 RecordSig = 0x43627364;

    time_hostid() {}
    time_hostid(ui32 h, ui32 ct)
        : HostId(h)
        , CrawlTime(ct)
    {}

    bool operator < (const time_hostid &b) const {
        return CrawlTime > b.CrawlTime || (CrawlTime == b.CrawlTime && HostId < b.HostId);
    }
};

struct TNewQueuedUrls {
    ui64 UrlId;
    double Rank;
    ui32 HostId;
    ui32 CrawlTime;
    ui8  Policy;
    ui8  RobotZoneCode;

    TNewQueuedUrls() {
    }

    TNewQueuedUrls(ui32 hostId, ui64 urlId, ui32 crawlTime, ui8 policy, ui8 robotZoneCode, double rank)
        : UrlId(urlId)
        , Rank(rank)
        , HostId(hostId)
        , CrawlTime(crawlTime)
        , Policy(policy)
        , RobotZoneCode(robotZoneCode)
    {}

    static const ui32 RecordSig = 0xB0D676B9;

    static int ByUid(const TNewQueuedUrls* lhs, const TNewQueuedUrls* rhs) {
        return CompareUids(lhs, rhs);
    }

    static int ByRank(const TNewQueuedUrls* lhs, const TNewQueuedUrls* rhs) {
        return -(::compare(lhs->Rank, rhs->Rank));
    }

    static int ByTime(const TNewQueuedUrls* lhs, const TNewQueuedUrls* rhs) {
        return lhs->CrawlTime - rhs->CrawlTime;  // reverse
    }

    static int ByTimeUid(const TNewQueuedUrls* lhs, const TNewQueuedUrls* rhs) {
        int ret = ByTime(lhs, rhs);
        return ret ? ret : CompareUids(lhs, rhs);
    }

    static int ByUidTime(const TNewQueuedUrls* lhs, const TNewQueuedUrls* rhs) {
        int ret = CompareUids(lhs, rhs);
        return ret ? ret : ByTime(lhs, rhs);
    }

    static int ByDescentTimeUid(const TNewQueuedUrls* lhs, const TNewQueuedUrls* rhs) {
        int ret = ByTime(rhs, lhs);
        return ret ? ret : CompareUids(lhs, rhs);
    }

    operator time_hostid() const {
        return time_hostid(HostId, CrawlTime);
    }
};

MAKESORTERTMPL(TNewQueuedUrls, ByUid);
MAKESORTERTMPL(TNewQueuedUrls, ByRank);
MAKESORTERTMPL(TNewQueuedUrls, ByTime);
MAKESORTERTMPL(TNewQueuedUrls, ByTimeUid);
MAKESORTERTMPL(TNewQueuedUrls, ByUidTime);
MAKESORTERTMPL(TNewQueuedUrls, ByDescentTimeUid);

struct TNewQueuedUrlsOld {
    ui64 UrlId;
    double Rank;
    ui32 HostId;
    ui32 CrawlTime;
    ui8  Policy;

    TNewQueuedUrlsOld() {
    }

    TNewQueuedUrlsOld(ui32 hostId, ui64 urlId, ui32 crawlTime, ui8 policy, double rank)
        : UrlId(urlId)
        , Rank(rank)
        , HostId(hostId)
        , CrawlTime(crawlTime)
        , Policy(policy)
    {}

    static const ui32 RecordSig = 0xB0D676B8;

    static int ByUid(const TNewQueuedUrlsOld* lhs, const TNewQueuedUrlsOld* rhs) {
        return CompareUids(lhs, rhs);
    }

    static int ByTime(const TNewQueuedUrlsOld* lhs, const TNewQueuedUrlsOld* rhs) {
        return lhs->CrawlTime - rhs->CrawlTime;  // reverse
    }

    static int ByTimeUid(const TNewQueuedUrlsOld* lhs, const TNewQueuedUrlsOld* rhs) {
        int ret = ByTime(lhs, rhs);
        return ret ? ret : CompareUids(lhs, rhs);
    }

    static int ByUidTime(const TNewQueuedUrlsOld* lhs, const TNewQueuedUrlsOld* rhs) {
        int ret = CompareUids(lhs, rhs);
        return ret ? ret : ByTime(lhs, rhs);
    }

    static int ByDescentTimeUid(const TNewQueuedUrlsOld* lhs, const TNewQueuedUrlsOld* rhs) {
        int ret = ByTime(rhs, lhs);
        return ret ? ret : CompareUids(lhs, rhs);
    }

    operator time_hostid() const {
        return time_hostid(HostId, CrawlTime);
    }
};

MAKESORTERTMPL(TNewQueuedUrlsOld, ByUid);
MAKESORTERTMPL(TNewQueuedUrlsOld, ByTime);
MAKESORTERTMPL(TNewQueuedUrlsOld, ByTimeUid);
MAKESORTERTMPL(TNewQueuedUrlsOld, ByUidTime);
MAKESORTERTMPL(TNewQueuedUrlsOld, ByDescentTimeUid);

struct TUidRankZone {
    ui64 UrlId;
    double Rank;
    ui32 HostId;
    ui8 Zone;

    TUidRankZone()
        {
        }
    TUidRankZone(ui32 hostId, ui64 urlId, double rank, ui32 zone)
        : UrlId(urlId)
        , Rank(rank)
        , HostId(hostId)
        , Zone(static_cast<ui8>(zone))
    {
    }
    TUidRankZone(const TUidRankZone* rec)
        : UrlId(rec->UrlId)
        , Rank(rec->Rank)
        , HostId(rec->HostId)
        , Zone(rec->Zone)
    {
    }

    static const ui32 RecordSig = 0xA438EF81;

    static int ByUid(const TUidRankZone* lhs, const TUidRankZone* rhs) {
        return CompareUids(lhs, rhs);
    }

    static int ByRank(const TUidRankZone* lhs, const TUidRankZone* rhs) {
        return -(::compare(lhs->Rank, rhs->Rank));
    }

    static int ByRankUid(const TUidRankZone* lhs, const TUidRankZone* rhs) {
        int ret = ByRank(lhs, rhs);
        return ret ? ret : ByUid(lhs, rhs);
    }

    static int ByUidZone(const TUidRankZone* lhs, const TUidRankZone* rhs) {
        int ret = ByUid(lhs, rhs);
        return ret ? ret : ::compare(lhs->Zone, rhs->Zone);
    }
    operator urlid_t () const {
        return urlid_t(HostId, UrlId);
    }
    size_t SizeOf() const {
        return 21;
    }
};

MAKESORTERTMPL(TUidRankZone, ByUid);
MAKESORTERTMPL(TUidRankZone, ByRank);
MAKESORTERTMPL(TUidRankZone, ByRankUid);
MAKESORTERTMPL(TUidRankZone, ByUidZone);

struct TUrlIdRankRec {
    ui32 Number;
    ui32 Zone;
    ui32 HostId;
    ui64 UrlId;
    double Rank;
    ui8 Status;

    TUrlIdRankRec() {
    }

    TUrlIdRankRec(ui32 hostId, ui64 urlId, double rank, ui32 zone, ui32 num = 0, ui32 status = 0)
        : Number(num)
        , Zone(zone)
        , HostId(hostId)
        , UrlId(urlId)
        , Rank(rank)
        , Status((ui8)status)
    {
    }

    TUrlIdRankRec(const TUrlIdRankRec* rec, ui32 status)
        : Number(rec->Number)
        , Zone(rec->Zone)
        , HostId(rec->HostId)
        , UrlId(rec->UrlId)
        , Rank(rec->Rank)
        , Status((ui8)status)
    {
    }

    static const ui32 RecordSig = 0x19AE4188;

    bool operator< (const TUrlIdRankRec& rhs) const {
        return ByUid(this, &rhs) < 0;
    }

    static int ByUid(const TUrlIdRankRec* lhs, const TUrlIdRankRec* rhs) {
        return CompareUids(lhs, rhs);
    }

    static int ByRank(const TUrlIdRankRec* lhs, const TUrlIdRankRec* rhs) {
        return -(::compare(lhs->Rank, rhs->Rank));
    }

    static int ByRankUid(const TUrlIdRankRec* lhs, const TUrlIdRankRec* rhs) {
        int ret = ByRank(lhs, rhs);
        return ret ? ret : ByUidZone(lhs, rhs);
    }

    static int ByUidZone(const TUrlIdRankRec* lhs, const TUrlIdRankRec* rhs) {
        int ret = ByUid(lhs, rhs);
        return ret ? ret : ::compare(lhs->Zone, rhs->Zone);
    }

};

MAKESORTERTMPL(TUrlIdRankRec, ByUid);
MAKESORTERTMPL(TUrlIdRankRec, ByRank);
MAKESORTERTMPL(TUrlIdRankRec, ByRankUid);
MAKESORTERTMPL(TUrlIdRankRec, ByUidZone);

struct TDelReasonRec {
    ui32 Reason;
    ui32 DocId;

    static const ui32 RecordSig = 0xB71EC46A;
};

struct TUidOrphan {
    ui64 UrlId;
    ui32 HostId;
    ui32 Time;

    static const ui32 RecordSig = 0xD19F37EC;

    static int ByUid(const TUidOrphan* a, const TUidOrphan* b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TUidOrphan, ByUid);

struct TSrcUrlId {
    ui64 UrlId;

    static const ui32 RecordSig = 0xABC123E1;

    static int ByUrlId(const TSrcUrlId* a, const TSrcUrlId* b) {
        return ::compare(a->UrlId, b->UrlId);
    }

    static int AreEqual(const TSrcUrlId* a, const TSrcUrlId* b) {
        return (a->UrlId == b->UrlId) ? -1 : 0;
    }
};

MAKESORTERTMPL(TSrcUrlId, ByUrlId);
MAKESORTERTMPL(TSrcUrlId, AreEqual);

struct TSrcUrlIdSieve {
    static int Sieve(TSrcUrlId *prev, const TSrcUrlId *next) {
        if (TSrcUrlId::ByUrlId(prev, next))
            return 0;
        else
            return 1;
    }
};

/*struct TCandRec {
    ui32            HostId;
    ui64            UrlId;
    bool            Allowed;
    bool            Queued;
    double           Weight;

    static const ui32 RecordSig = 0xBEEF1001;

    urlid_t Uid() {
        urlid_t uid;
        uid.HostId = HostId;
        uid.UrlId = UrlId;
        return uid;
    }

    static int ByUid(const TCandRec *a, const TCandRec *b) {
        return ::CompareUids(a, b);
    }
};

MAKESORTERTMPL(TCandRec, ByUid);*/

struct TUidRank {
    ui32 HostId;
    ui64 UrlId;
    double Rank;

    static const ui32 RecordSig = 0xFEEDDEED;

    TUidRank(ui32 hostid, ui64 urlid, double rank)
        : HostId(hostid)
        , UrlId(urlid)
        , Rank(rank)
    {}

    TUidRank(const TUrlRecNoProto* rec, double rank, bool isnew = false, ui8 clientType = 0)
        : HostId(rec->HostId)
        , UrlId(rec->UrlId)
        , Rank(rank)
    {
        Y_UNUSED(clientType);
        Y_UNUSED(isnew);
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TUidRank *a, const TUidRank *b) {
        return ::CompareUids(a, b);
    }

    static int ByRank(const TUidRank *u1, const TUidRank *u2) {
        return -(::compare(u1->Rank, u2->Rank));
    }
};

MAKESORTERTMPL(TUidRank, ByUid);
MAKESORTERTMPL(TUidRank, ByRank);

struct TPreQueue {
    ui64 UrlId;
    double Rank;
    TDataworkDate ModTime;
    ui32 HostId;
    ui32 Flags;
    ui32 RankNum;
    ui16 NeedToUpdateLangRegion;
    ui8 LangRegion;
    ui8 Zone;
    ui8 Hops;
    ui8 Status;
    ui8 ClientType;
    ui8 IsNewPolicy;
    url_t Name;

    static const ui32 RecordSig = 0xFADE0135;

    TPreQueue(const TUrlRecNoProto * url, ui8 robotZoneCode, double rank, bool isNewPolicy, ui8 clientType = 0, ui16 langRegion = GetDefaultLangRegion(), ui16 needToUpdateLangRegion = 0)
        : UrlId(url->UrlId)
        , Rank(rank)
        , ModTime(url->ModTime)
        , HostId(url->HostId)
        , Flags(url->Flags)
        , RankNum(0)
        , NeedToUpdateLangRegion(needToUpdateLangRegion)
        , LangRegion(langRegion)
        , Zone(robotZoneCode)
        , Hops(url->Hops)
        , Status(url->Status)
        , ClientType(clientType)
    {
        IsNewPolicy = isNewPolicy ? 1 : 0;
        strcpy(Name, url->Name);
        if (url->MimeType == MIME_PDF && url->ModTime < 1301616000) // 1 April 2011 (not a joke)
            ModTime = 0; // Hack for pdf re-recognize (asked by fomichev@)
    }

    TPreQueue(const TPreQueue* url, ui32 rankNum)
        : UrlId(url->UrlId)
        , Rank(url->Rank)
        , ModTime(url->ModTime)
        , HostId(url->HostId)
        , Flags(url->Flags)
        , RankNum(rankNum)
        , NeedToUpdateLangRegion(url->NeedToUpdateLangRegion)
        , LangRegion(url->LangRegion)
        , Zone(url->Zone)
        , Hops(url->Hops)
        , Status(url->Status)
        , ClientType(url->ClientType)
        , IsNewPolicy(url->IsNewPolicy)
    {
        strcpy(Name, url->Name);
    }

    TPreQueue(const TPreQueue* url)
        : UrlId(url->UrlId)
        , Rank(url->Rank)
        , ModTime(url->ModTime)
        , HostId(url->HostId)
        , Flags(url->Flags)
        , RankNum(url->RankNum)
        , NeedToUpdateLangRegion(url->NeedToUpdateLangRegion)
        , LangRegion(url->LangRegion)
        , Zone(url->Zone)
        , Hops(url->Hops)
        , Status(url->Status)
        , ClientType(url->ClientType)
        , IsNewPolicy(url->IsNewPolicy)
    {
        strcpy(Name, url->Name);
    }

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByUid(const TPreQueue *a, const TPreQueue *b) {
        return ::CompareUids(a, b);
    }

    static int ByRank(const TPreQueue *a, const TPreQueue *b) {
        return -(::compare(a->Rank, b->Rank));
    }
};

MAKESORTERTMPL(TPreQueue, ByUid);
MAKESORTERTMPL(TPreQueue, ByRank);

struct TPreQueueOld {
    ui64 UrlId;
    double Rank;
    TDataworkDate ModTime;
    ui32 HostId;
    ui32 Flags;
    ui32 RankNum;
    ui16 NeedToUpdateLangRegion;
    ui16 LangRegion; // really needed only ui8, replace if you need more ui8 fields
    ui8 Hops;
    ui8 Status;
    ui8 ClientType;
    ui8 IsNewPolicy;
    url_t Name;

    static const ui32 RecordSig = 0xFADE0134;

    TPreQueueOld(const TUrlRecNoProto * url, double rank, bool isNewPolicy, ui8 clientType = 0, ui16 langRegion = GetDefaultLangRegion(), ui16 needToUpdateLangRegion = 0)
        : UrlId(url->UrlId)
        , Rank(rank)
        , ModTime(url->ModTime)
        , HostId(url->HostId)
        , Flags(url->Flags)
        , RankNum(0)
        , NeedToUpdateLangRegion(needToUpdateLangRegion)
        , LangRegion(langRegion)
        , Hops(url->Hops)
        , Status(url->Status)
        , ClientType(clientType)
    {
        IsNewPolicy = isNewPolicy ? 1 : 0;
        strcpy(Name, url->Name);
        if (url->MimeType == MIME_PDF && url->ModTime < 1301616000) // 1 April 2011 (not a joke)
            ModTime = 0; // Hack for pdf re-recognize (asked by fomichev@)
    }

    TPreQueueOld(const TPreQueueOld* url, ui32 rankNum)
        : UrlId(url->UrlId)
        , Rank(url->Rank)
        , ModTime(url->ModTime)
        , HostId(url->HostId)
        , Flags(url->Flags)
        , RankNum(rankNum)
        , NeedToUpdateLangRegion(url->NeedToUpdateLangRegion)
        , LangRegion(url->LangRegion)
        , Hops(url->Hops)
        , Status(url->Status)
        , ClientType(url->ClientType)
        , IsNewPolicy(url->IsNewPolicy)
    {
        strcpy(Name, url->Name);
    }

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByUid(const TPreQueueOld *a, const TPreQueueOld *b) {
        return ::CompareUids(a, b);
    }

    static int ByRank(const TPreQueueOld *a, const TPreQueueOld *b) {
        return -(::compare(a->Rank, b->Rank));
    }
};

MAKESORTERTMPL(TPreQueueOld, ByUid);
MAKESORTERTMPL(TPreQueueOld, ByRank);

struct TLastQueueStat {
    ui64 UrlId;
    double Rank;
    ui32 HostId;
    ui32 PolicyId;
    ui32 QueueId;
    ui8  RobotZoneCode;
    ui8 Status;
    ui8 IsNewPolicy;

    static const ui32 RecordSig = 0xABBA2013;

    TLastQueueStat(const TPreQueue* url, ui32 policyId, ui8 robotZoneCode, ui32 queueId)
        : UrlId(url->UrlId)
        , Rank(url->Rank)
        , HostId(url->HostId)
        , PolicyId(policyId)
        , QueueId(queueId)
        , RobotZoneCode(robotZoneCode)
        , Status(url->Status)
        , IsNewPolicy(url->IsNewPolicy)
    {}

    static int ByUid(const TLastQueueStat *a, const TLastQueueStat *b) {
        return ::CompareUids(a, b);
    }

    static int ByRank(const TLastQueueStat *a, const TLastQueueStat *b) {
        return -(::compare(a->Rank, b->Rank));
    }
};

MAKESORTERTMPL(TLastQueueStat, ByUid);
MAKESORTERTMPL(TLastQueueStat, ByRank);

struct TLastQueueStatUnstable {
    ui64 UrlId;
    double Rank;
    ui32 HostId;
    ui32 PolicyId;
    ui8  RobotZoneCode;
    ui8 Dummy1; ui8 Dummy2; ui8 Dummy3; // Needed due to the 32-bit field alignment
    ui32 QueueId;
    ui8 Status;
    ui8 IsNewPolicy;

    static const ui32 RecordSig = 0xABBA2012;

    TLastQueueStatUnstable(const TPreQueue* url, ui32 policyId, ui8 robotZoneCode, ui32 queueId)
        : UrlId(url->UrlId)
        , Rank(url->Rank)
        , HostId(url->HostId)
        , PolicyId(policyId)
        , RobotZoneCode(robotZoneCode)
        , QueueId(queueId)
        , Status(url->Status)
        , IsNewPolicy(url->IsNewPolicy)
    {}

    static int ByUid(const TLastQueueStatUnstable *a, const TLastQueueStatUnstable *b) {
        return ::CompareUids(a, b);
    }

    static int ByRank(const TLastQueueStatUnstable *a, const TLastQueueStatUnstable *b) {
        return -(::compare(a->Rank, b->Rank));
    }
};

MAKESORTERTMPL(TLastQueueStatUnstable, ByUid);
MAKESORTERTMPL(TLastQueueStatUnstable, ByRank);

struct TLastQueueStatOld {
    ui64 UrlId;
    double Rank;
    ui32 HostId;
    ui32 PolicyId;
    ui32 QueueId;
    ui8 Status;
    ui8 IsNewPolicy;

    static const ui32 RecordSig = 0xABBA2011;

    TLastQueueStatOld(const TPreQueue* url, ui32 policyId, ui32 queueId)
        : UrlId(url->UrlId)
        , Rank(url->Rank)
        , HostId(url->HostId)
        , PolicyId(policyId)
        , QueueId(queueId)
        , Status(url->Status)
        , IsNewPolicy(url->IsNewPolicy)
    {}

    static int ByUid(const TLastQueueStatOld *a, const TLastQueueStatOld *b) {
        return ::CompareUids(a, b);
    }

    static int ByRank(const TLastQueueStatOld *a, const TLastQueueStatOld *b) {
        return -(::compare(a->Rank, b->Rank));
    }
};

MAKESORTERTMPL(TLastQueueStatOld, ByUid);
MAKESORTERTMPL(TLastQueueStatOld, ByRank);

struct TQueueStat {
    ui64 UrlId;
    double Rank;
    ui32 HostId;
    ui32 Number;
    ui32 QueueTime;
    ui8 RobotZoneCode;
    ui8 Status;

    static const ui32 RecordSig = 0xABCD4245;

    TQueueStat(const TLastQueueStat* rec, ui32 queuetime)
        : UrlId(rec->UrlId)
        , Rank(rec->Rank)
        , HostId(rec->HostId)
        , Number(rec->PolicyId)
        , QueueTime(queuetime)
        , RobotZoneCode(rec->RobotZoneCode)
        , Status(rec->Status)
    {
    }

    static int ByUid(const TQueueStat *a, const TQueueStat *b) {
        return ::CompareUids(a, b);
    }
};

MAKESORTERTMPL(TQueueStat, ByUid);

struct TIpValue {
    ui32 Ip;
    ui32 Value;

    static const ui32 RecordSig = 0xBADACC07;

    TIpValue()
        : Ip(0)
        , Value(0)
    {
    }

    TIpValue(ui32 ip, ui32 value)
        : Ip(ip)
        , Value(value)
    {
    }

    static int ByIp(const TIpValue* a, const TIpValue* b) {
        return ::compare(a->Ip, b->Ip);
    }
};

MAKESORTERTMPL(TIpValue, ByIp);

struct TIpQueuedLimit {
    ui32 Ip;
    ui32 Queued;
    ui32 Limit;

    static const ui32 RecordSig = 0xBADACC87;

    TIpQueuedLimit()
        : Ip(0)
        , Queued(0)
        , Limit(0)
    {
    }

    TIpQueuedLimit(ui32 ip, ui32 queued, ui32 limit)
        : Ip(ip)
        , Queued(queued)
        , Limit(limit)
    {
    }

    static int ByIp(const TIpQueuedLimit* a, const TIpQueuedLimit* b) {
        return ::compare(a->Ip, b->Ip);
    }
};

MAKESORTERTMPL(TIpQueuedLimit, ByIp);

struct TIpStatRecOld {
    ui32 Ip;
    ui32 Queued;
    ui32 Limit;
    ui32 Planned;
    ui32 Domains;
    ui32 Time;

    static const ui32 RecordSig = 0x87AB93DF;

    TIpStatRecOld()
        : Ip(0)
        , Queued(0)
        , Limit(0)
        , Planned(0)
        , Domains(0)
        , Time(0)
    {
    }

    TIpStatRecOld(ui32 ip, ui32 queued, ui32 limit, ui32 planned, ui32 domains, ui32 time)
        : Ip(ip)
        , Queued(queued)
        , Limit(limit)
        , Planned(planned)
        , Domains(domains)
        , Time(time)
    {
    }

    static int ByIp(const TIpStatRecOld* a, const TIpStatRecOld* b) {
        return ::compare(a->Ip, b->Ip);
    }
};

MAKESORTERTMPL(TIpStatRecOld, ByIp);

struct TIpStatRec {
    ui32 Ip;
    ui32 Queued;
    ui32 Limit;
    ui32 Planned;
    ui32 Domains;
    ui32 Time;
    ui32 NewUrls;
    ui32 Docs;

    static const ui32 RecordSig = 0x87AB93E0;

    TIpStatRec()
        : Ip(0)
        , Queued(0)
        , Limit(0)
        , Planned(0)
        , Domains(0)
        , Time(0)
        , NewUrls(0)
        , Docs(0)
    {
    }

    TIpStatRec(ui32 ip, ui32 queued, ui32 limit, ui32 planned, ui32 domains, ui32 time, ui32 newUrls, ui32 docs)
        : Ip(ip)
        , Queued(queued)
        , Limit(limit)
        , Planned(planned)
        , Domains(domains)
        , Time(time)
        , NewUrls(newUrls)
        , Docs(docs)
    {
    }

    static int ByIp(const TIpStatRec* a, const TIpStatRec* b) {
        return ::compare(a->Ip, b->Ip);
    }
};

MAKESORTERTMPL(TIpStatRec, ByIp);

struct TQueueNum {
    ui64 UrlId;
    ui32 HostId;
    ui32 CrawlTime;
    ui32 Number;

    static const ui32 RecordSig = 0xBACABACA;
};

struct THostQStat {
    ui32 HostId;
    ui32 AllPushed;
    ui32 AfterFilter;
    ui32 AfterBound;
    ui32 AfterLimiter;
    ui32 Time;
    ui32 Policy;
    ui32 RobotZoneCode;  // actually need only 8 bits

    static const ui32 RecordSig = 0xDEADC0DE;

    THostQStat()
        : HostId(0)
        , AllPushed(0)
        , AfterFilter(0)
        , AfterBound(0)
        , AfterLimiter(0)
        , Time(0)
        , Policy(0)
        , RobotZoneCode(0)
    {}

    THostQStat(const THostQStat* rec, ui32 time)
        : HostId(rec->HostId)
        , AllPushed(rec->AllPushed)
        , AfterFilter(rec->AfterFilter)
        , AfterBound(rec->AfterBound)
        , AfterLimiter(rec->AfterLimiter)
        , Time(time)
        , Policy(rec->Policy)
        , RobotZoneCode(rec->RobotZoneCode)
    {}

    operator hostid_t() const
    {
        return HostId;
    }

    static int ByHostId(const THostQStat* a, const THostQStat* b) {
        return compare(a->HostId, b->HostId);
    }
};

MAKESORTERTMPL(THostQStat, ByHostId);

struct THostQStatOld {
    ui32 HostId;
    ui32 AllPushed;
    ui32 AfterFilter;
    ui32 AfterBound;
    ui32 AfterLimiter;
    ui32 Time;
    ui32 Policy;

    static const ui32 RecordSig = 0xDEADC0DF;

    THostQStatOld()
        : HostId(0)
        , AllPushed(0)
        , AfterFilter(0)
        , AfterBound(0)
        , AfterLimiter(0)
        , Time(0)
        , Policy(0)
    {}

    THostQStatOld(const THostQStatOld* rec, ui32 time)
        : HostId(rec->HostId)
        , AllPushed(rec->AllPushed)
        , AfterFilter(rec->AfterFilter)
        , AfterBound(rec->AfterBound)
        , AfterLimiter(rec->AfterLimiter)
        , Time(time)
        , Policy(rec->Policy)
    {}

    operator hostid_t() const
    {
        return HostId;
    }

    static int ByHostId(const THostQStatOld* a, const THostQStatOld* b) {
        return compare(a->HostId, b->HostId);
    }
};

MAKESORTERTMPL(THostQStatOld, ByHostId);

struct TDeletedHostInfo {
    ui32   HostId;
    ui32   AddTime;
    ui32   LastAccess;
    ui32   Penalty;
    ui32   DeleteTime;
    ui32   FilterCode;
    dbscheeme::host_t Name;

    static const ui32 RecordSig = 0xDADEC0DE;

    static int ByMinusDeleteTime(const TDeletedHostInfo* a, const TDeletedHostInfo* b) {
        return ::compare(-1 * a->DeleteTime, -1 * b->DeleteTime);
    }

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }
};

MAKESORTERTMPL(TDeletedHostInfo, ByMinusDeleteTime);

struct TPreparatLink {
    ui64        SrcUrlId;
    ui64        DstUrlId;
    ui64        Fnv;
    ui32        SrcOwnerId;
    ui32        SrcHostId;
    ui32        DstHostId;
    ui32        Flags;
    ui32        SeoFlags;
    ui16        DateDiscovered;
    ui16        SrcSeg;     // Act like destination
    ui16        DataLen;    // For internal use
    ui8         SrcLang;
    redir_t     Text;

    static const ui32 RecordSig = 0xB4C90618;

    enum { HOST_TO = 1, LINK_TEXT = 2 };

public:
    TPreparatLink() {
        memset(Text, 0, sizeof(ui64));
    }

    size_t SizeOf() const {
        return offsetofthis(Text) + DataLen;
    }

    const char* GetText(char code) const {
        if (DataLen) {
            const char* p = (char*)Text;
            if (*p == code)
                return p + 1;
            p += 1 + strlen(p + 1) + 1;
            if (*p == code)
                return p + 1;
        }
        return nullptr;
    }

    void SetText(const char* text, char code) {
        char*  p     = (char*)Text;
        size_t len   = strlen(text);
        if (*p != 0)
            p += 1 + strlen(p + 1) + 1;
        *p = code; p++;
        strncpy(p, text, len); p += len;
        *p = '\0';
        DataLen += ui16(len + 2);
    }

    static int ByDstUid(const TPreparatLink* a, const TPreparatLink* b) {
        int ret = ::compare(a->DstHostId, b->DstHostId);
        return ret ? ret : ::compare(a->DstUrlId, b->DstUrlId);
    }

    static int ByDstUidSigned(const TPreparatLink* a, const TPreparatLink* b) {
        int ret = ::compare((i32)a->DstHostId, (i32)b->DstHostId);
        return ret ? ret : ::compare(a->DstUrlId, b->DstUrlId);
    }

    static int ByFnvSrc(const TPreparatLink* a, const TPreparatLink* b) {
        int ret = ::compare(a->Fnv, b->Fnv);
        ret = ret ? ret : ::compare(a->SrcOwnerId, b->SrcOwnerId);
        ret = ret ? ret : ::compare(a->DstHostId, b->DstHostId);
        return ret ? ret : ::compare(a->DstUrlId, b->DstUrlId);
    }

    static int ByFnvSrcHostSrcOwner(const TPreparatLink* a, const TPreparatLink* b) {
        int ret = ::compare(a->Fnv, b->Fnv);
        ret = ret ? ret : ::compare(a->SrcHostId, b->SrcHostId);
        return ret ? ret : ::compare(a->SrcOwnerId, b->SrcOwnerId);
    }
};

struct TPreparatLinkSieve {
    static int Sieve(TPreparatLink* prev, const TPreparatLink* next) {
        if (TPreparatLink::ByFnvSrc(prev, next))
            return 0;
        else
            return 1;
    }
};

MAKESORTERTMPL(TPreparatLink, ByDstUid);
MAKESORTERTMPL(TPreparatLink, ByDstUidSigned);
MAKESORTERTMPL(TPreparatLink, ByFnvSrc);
MAKESORTERTMPL(TPreparatLink, ByFnvSrcHostSrcOwner);

// store sum of document sizes (size can be in hits count) for each search zone, that needed for one search shard
struct TSearchZoneSizeRec {
    ui64 Size;
    ui8  SearchZoneCode;

    static const ui32 RecordSig = 0xB4C90622;

    TSearchZoneSizeRec(const ui8 searchZoneCode, const ui64 size)
        : Size(size)
        , SearchZoneCode(searchZoneCode)
    {}

};

struct TSearchZoneCompressionCoeffRec {
    double CompressionCoeff;
    ui8  SearchZoneCode;

    static const ui32 RecordSig = 0xB4C90623;

    TSearchZoneCompressionCoeffRec() {
    }

    TSearchZoneCompressionCoeffRec(const ui8 searchZoneCode, const double compressionCoeff)
        : CompressionCoeff(compressionCoeff)
        , SearchZoneCode(searchZoneCode)
    {}

};

struct TQualityDistributionRec {

    ui32   SegmentId;
    ui32   DocId;
    ui32   HitsCount;
    double UploadRank;

    TQualityDistributionRec(
        const ui32 segmentId,
        const ui32 docId,
        const ui32 hitsCount,
        const double uploadRank)
        : SegmentId(segmentId)
        , DocId(docId)
        , HitsCount(hitsCount)
        , UploadRank(uploadRank) {}

    static const ui32 RecordSig = 0xC4B92621;

    static int ByMinusUploadRankDocId(const TQualityDistributionRec* a, const TQualityDistributionRec* b) {
        int rankCompare = ::compare(-1.0 * a->UploadRank, -1.0 * b->UploadRank);
        if (rankCompare) return rankCompare;
        return ::compare(a->DocId, b->DocId);
    }

    static int BySegmentDocId(const TQualityDistributionRec* a, const TQualityDistributionRec* b) {
        int segmentCompare = ::compare(a->SegmentId, b->SegmentId);
        if (segmentCompare) return segmentCompare;
        return ::compare(a->DocId, b->DocId);
    }

};

MAKESORTERTMPL(TQualityDistributionRec, ByMinusUploadRankDocId);
MAKESORTERTMPL(TQualityDistributionRec, BySegmentDocId);

struct TPruningRec {

    ui32   Cluster;
    ui32   ClusterDocId;
    ui32   SearchZoneCode;
    double PruningRank;

    TPruningRec(
        const ui32 cluster,
        const ui32 clusterDocId,
        const ui32 searchZoneCode,
        const double pruningRank)
        : Cluster(cluster)
        , ClusterDocId(clusterDocId)
        , SearchZoneCode(searchZoneCode)
        , PruningRank(pruningRank) {}

    static const ui32 RecordSig = 0xC4B92126;

    static int ByClusterSearchZoneCodeDocId(const TPruningRec* a, const TPruningRec* b) {
        int clusterComparison = ::compare(a->Cluster, b->Cluster);
        if (clusterComparison) return clusterComparison;
        int searchZoneCodeComparison = ::compare(a->SearchZoneCode, b->SearchZoneCode);
        if (searchZoneCodeComparison) return searchZoneCodeComparison;
        return ::compare(a->ClusterDocId, b->ClusterDocId);
    }

};

MAKESORTERTMPL(TPruningRec, ByClusterSearchZoneCodeDocId);

struct TUploadDocRec {

    ui32   SegmentId;
    ui32   DocId;
    ui32   UploadRuleId;

    TUploadDocRec() {}

    TUploadDocRec(
        const ui32 segmentId,
        const ui32 docId,
        const ui32 uploadRuleId)
        : SegmentId(segmentId)
        , DocId(docId)
        , UploadRuleId(uploadRuleId) {}

    static const ui32 RecordSig = 0xC4B92622;

    static int BySegmentDocId(const TUploadDocRec* a, const TUploadDocRec* b) {
        int segmentCompare = ::compare(a->SegmentId, b->SegmentId);
        if (segmentCompare) return segmentCompare;
        return ::compare(a->DocId, b->DocId);
    }

};

struct TUploadDocOrderRec : public TUploadDocRec {
    ui32 Order;

    TUploadDocOrderRec() {}

    TUploadDocOrderRec(
        const ui32 segmentId,
        const ui32 docId,
        const ui32 uploadRuleId,
        const ui32 order)
        : TUploadDocRec(segmentId, docId, uploadRuleId)
        , Order(order) {}

    static const ui32 RecordSig = 0xC4B92623;
};

MAKESORTERTMPL(TUploadDocRec, BySegmentDocId);
MAKESORTERTMPL(TUploadDocOrderRec, BySegmentDocId);

struct TRusZonesStatRec {
    ui32   SegmentId;
    ui32   DocId;
    double Size;

    TRusZonesStatRec() {}

    TRusZonesStatRec(
        const ui32 segmentId,
        const ui32 docId,
        const double size)
        : SegmentId(segmentId)
        , DocId(docId)
        , Size(size) {}

    static const ui32 RecordSig = 0x453EA90F;
};

// for robot zone stored upload rank threshold and search zone to which docs with greater upload rank must be taken
struct TUploadRankThreshold {

    double UploadRankThreshold;
    ui8  RobotZoneCode;
    ui8  SearchZoneCode;

    static const ui32 RecordSig = 0x15558662;

    TUploadRankThreshold(const double uploadRankThreshold, const ui8 robotZoneCode, ui8 searchZoneCode)
        : UploadRankThreshold(uploadRankThreshold)
        , RobotZoneCode(robotZoneCode)
        , SearchZoneCode(searchZoneCode) {}

};

struct TComplexIdRec {
    ui64 Owner;
    ui64 Path;

    static const ui32 RecordSig = 0x13338770;

    TComplexIdRec(ui64 owner = 0, ui64 path = 0)
        : Owner(owner)
        , Path(path)
    {
    }

    bool operator < (const TComplexIdRec& r) const {
        if (Owner < r.Owner)
            return true;
        if (Owner > r.Owner)
            return false;
        return Path < r.Path;
    }
};

struct TPolicyStat {
    ui32 Policy;
    ui32 CrawlTime;
    ui32 ZeroIp;
    ui32 Deleted;
    ui32 CrawledMorda;
    ui32 QueuedMorda;
    ui32 Crawled;
    ui32 Queued;

    static const ui32 RecordSig = 0xC0B0B0D1;

    TPolicyStat(ui32 policy, ui32 crawltime, ui32 zeroip, ui32 deleted, ui32 cmorda, ui32 qmorda, ui32 crawled, ui32 queued)
        : Policy(policy)
        , CrawlTime(crawltime)
        , ZeroIp(zeroip)
        , Deleted(deleted)
        , CrawledMorda(cmorda)
        , QueuedMorda(qmorda)
        , Crawled(crawled)
        , Queued(queued)
    {
    }
};

struct THubRec: public urlid_t {
    ui16 NewLinksCount;
    TDataworkDate AddTime;

    static const ui32 RecordSig = 0x23028701;

    static int ByUidAndTime(const THubRec *a, const THubRec *b) {
        int c = ByUid(a, b);
        if (c) return c;
        return ::compare(b->AddTime, a->AddTime);
    }
};

MAKESORTERTMPL(THubRec, ByUid);
MAKESORTERTMPL(THubRec, ByUidAndTime);

struct TYabarRec: public urlid_t {
    ui32 FreqStatus;
    ui32 FromYandexStatus;
    ui32 TransitionsCount;
    ui32 FromGoogleTransitionsCount;
    ui32 FromGoogleTransitions1Month;

    static const ui32 RecordSig = 0x23028702;
};

MAKESORTERTMPL(TYabarRec, ByUid);

struct TClickedLinkRec {
    ui64 UrlIdFrom;
    ui64 UrlIdTo;
    ui32 HostIdFrom;
    ui32 HostIdTo;
    double CTR;
    ui32 Clicks;
    ui16 OnSrcTime;
    ui16 OnDstTime;

    static const ui32 RecordSig = 0x4624FC21;

    static int ByUid(const TClickedLinkRec* a, const TClickedLinkRec* b) {
        int ret = ::compare(a->HostIdFrom, b->HostIdFrom);
        ret = ret ? ret : ::compare(a->UrlIdFrom, b->UrlIdFrom);
        ret = ret ? ret : ::compare(a->HostIdTo, b->HostIdTo);
        ret = ret ? ret : ::compare(a->UrlIdTo, b->UrlIdTo);
        return ret;
    }
};

MAKESORTERTMPL(TClickedLinkRec, ByUid);

struct TUrlScdataRec: public urlid_t {
    ui32 UrlShowsDay;
    ui32 UrlClicksDay;
    ui32 UrlShowsWeekly;
    ui32 UrlClicksWeekly;
    ui32 UrlShows3Month;
    ui32 UrlClicks3Month;

    static const ui32 RecordSig = 0x23029A01;
};

MAKESORTERTMPL(TUrlScdataRec, ByUid);

struct TOwnerScdataRec: public urlid_t {
    ui32 OwnerShowsDay;
    ui32 OwnerClicksDay;
    ui32 OwnerShowsWeekly;
    ui32 OwnerClicksWeekly;
    ui32 OwnerShows3Month;
    ui32 OwnerClicks3Month;

    static const ui32 RecordSig = 0x23028704;
};

MAKESORTERTMPL(TOwnerScdataRec, ByUid);

struct TExternalMRRec: public urlid_t {
    ui32 PosInAlexa;
    ui8 InAlexa;
    ui8 InGoogleTop1000;
    ui8 InDMOZ;

    static const ui32 RecordSig = 0x23028705;
};

MAKESORTERTMPL(TExternalMRRec, ByUid);

struct TJudgementRec: public urlid_t {
    ui8 Language;
    ui8 Judgement;

    static const ui32 RecordSig = 0x23028706;

    static int ByUidJudgement(const TJudgementRec *a, const TJudgementRec *b) {
        int ret = CompareUids(a, b);
        if (ret != 0) {
            return ret;
        } else if (b->Judgement == 0) {
            return -1;
        } else if (a->Judgement == 0) {
            return 1;
        } else {
            return ::compare(a->Judgement, b->Judgement);
        }
    }
};

MAKESORTERTMPL(TJudgementRec, ByUid);
MAKESORTERTMPL(TJudgementRec, ByUidJudgement);

struct TWikipediaExtLinksRec: public urlid_t {
    ui8 Language;
    ui8 Dummy1; ui8 Dummy2; ui8 Dummy3; // Needed due to the 32-bit field alignment
    ui32 Links;

    static const ui32 RecordSig = 0x23028707;
};

MAKESORTERTMPL(TWikipediaExtLinksRec, ByUid);

struct TUidPolicy {
    ui64 UrlId;
    ui32 HostId;
    ui32 Policy;
    ui8  RobotZoneCode;
    ui8 Dummy1; ui8 Dummy2; ui8 Dummy3; // Needed due to the 32-bit field alignment
    ui32 QueueId;

    static const ui32 RecordSig = 0xFEEDDEAF;

    TUidPolicy(ui32 hostid, ui64 urlid, ui32 policy, ui8 robotZoneCode, ui32 queueId)
        : UrlId(urlid)
        , HostId(hostid)
        , Policy(policy)
        , RobotZoneCode(robotZoneCode)
        , QueueId(queueId)
    {}

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TUidPolicy *a, const TUidPolicy *b) {
        return ::CompareUids(a, b);
    }
};

MAKESORTERTMPL(TUidPolicy, ByUid);

struct TUrlCrawlData : public urlid_t {
    typedef TUrlExtCrawlData TExtInfo;
    static const ui32 RecordSig = 0xDABADA02;
};

MAKESORTERTMPL(TUrlCrawlData, ByUid);

struct TAliveHostStat {
    double AlivePart;
    ui32 HostId;
    ui32 Crawled;

    static const ui32 RecordSig = 0xBAFF1234;

    operator hostid_t() const {
        return HostId;
    }
};

struct TUrlNameStatus {
    ui64 UrlId;
    ui32 HostId;
    ui32 HttpCode;
    ui8 Status;
    url_t Name;

    static const ui32 RecordSig = 0xABBACE01;

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }
};

struct TNumRefsFromMPRec {
    ui32    DocId;
    ui32    NumRefs;

    static const ui32 RecordSig = 0x28012011;

    TNumRefsFromMPRec(const ui32 docId = 0, const ui32 numRefs = 0)
        : DocId(docId)
        , NumRefs(numRefs)
    {
    }

    static int ByDocId(const TNumRefsFromMPRec* r1, const TNumRefsFromMPRec* r2)
    {
        const int ret = ::compare(r1->DocId, r2->DocId);
        return ret ? ret : ::compare(r1->NumRefs, r2->NumRefs);
    }
};

MAKESORTERTMPL(TNumRefsFromMPRec, ByDocId);
struct TDocArchiveSize {
    ui32 DocId;
    ui32 TagDocSize;
    ui32 PlainDocSize;

    static const ui32 RecordSig = 0x18143647;

    TDocArchiveSize()
        : DocId(0)
        , TagDocSize(0)
        , PlainDocSize(0)
    {
    }
    TDocArchiveSize(ui32 docId, ui32 tagDocSize, ui32 plainDocSize)
        : DocId(docId)
        , TagDocSize(tagDocSize)
        , PlainDocSize(plainDocSize)
    {
    }
};

struct THostIdDate {
    ui32 HostId;
    ui32 Date;

    static const ui32 RecordSig = 0x09051945;

    THostIdDate(ui32 hostid, ui32 date)
        : HostId(hostid)
        , Date(date)
    {
    }

//    static int ByHostId(const THostIdDate *a, const THostIdDate *b) {
//        return ::compare(a->HostId, b->HostId);
//    }
};

//MAKESORTERTMPL(THostIdDate, ByHostId);

struct TShowsSelRankRec {
    ui32 Shows;
    double SelRank;

    static const ui32 RecordSig = 0xFDF51901;

    TShowsSelRankRec(ui32 shows, double selRank)
        : Shows(shows)
        , SelRank(selRank)
    { }

    TShowsSelRankRec()
        : Shows(0)
        , SelRank(0)
    { }

    static int BySelRank(const TShowsSelRankRec* rec1, const TShowsSelRankRec* rec2) {
        return ::compare(rec2->SelRank, rec1->SelRank);
    }
};
MAKESORTERTMPL(TShowsSelRankRec, BySelRank);

struct TUrlSitemapPair {
    ui32 HostId;
    ui64 UrlId;
    ui32 SMHostId;
    ui64 SMUrlId;

    static const ui32 RecordSig = 0xB0101242; //The battle of the Ice 1242

    TUrlSitemapPair()
    : HostId(0)
    , UrlId(0)
    , SMHostId(0)
    , SMUrlId(0)
    { }


    static int ByUrl(const TUrlSitemapPair* rec1, const TUrlSitemapPair* rec2) {
        int ret = CompareUids(rec1, rec2);
        return ret ? ret : CompareSM(rec1, rec2);
    }

    static int BySitemap(const TUrlSitemapPair* rec1, const TUrlSitemapPair* rec2) {
        int ret = CompareSM(rec1, rec2);
        return ret ? ret : CompareUids(rec1, rec2);
    }

    inline static int CompareSM(const TUrlSitemapPair* rec1, const TUrlSitemapPair* rec2) {
        int ret = ::compare(rec1->SMHostId, rec2->SMHostId);
        return ret ? ret : ::compare(rec1->SMUrlId, rec2->SMUrlId);
    }
};

MAKESORTERTMPL(TUrlSitemapPair, ByUrl);
MAKESORTERTMPL(TUrlSitemapPair, BySitemap);
MAKESORTERTMPL(TUrlSitemapPair, CompareSM);

struct TProcessedUrlSitemapPair {
    ui32 HostId;
    ui64 UrlId;
    ui32 SMHostId;
    ui64 SMUrlId;

    i32 ModTime;
    i32 LastAccess;
    i32 BeforeLastAccess;
    i32 LastModified;

    static const ui32 RecordSig = 0xB0C01380; //The battle of Kulikovo 1380

    TProcessedUrlSitemapPair()
    : HostId(0)
    , UrlId(0)
    , SMHostId(0)
    , SMUrlId(0)
    , ModTime(0)
    , LastAccess(0)
    , BeforeLastAccess(0)
    { }

    static int ByUrl(const TProcessedUrlSitemapPair* rec1, const TProcessedUrlSitemapPair* rec2) {
        int ret = CompareUids(rec1, rec2);
        return ret ? ret : CompareSM(rec1, rec2);
    }

    static int BySitemap(const TProcessedUrlSitemapPair* rec1, const TProcessedUrlSitemapPair* rec2) {
        int ret = CompareSM(rec1, rec2);
        return ret ? ret : CompareUids(rec1, rec2);
    }

    inline static int CompareSM(const TProcessedUrlSitemapPair* rec1, const TProcessedUrlSitemapPair* rec2) {
        int ret = ::compare(rec1->SMHostId, rec2->SMHostId);
        return ret ? ret : ::compare(rec1->SMUrlId, rec2->SMUrlId);
    }
};

MAKESORTERTMPL(TProcessedUrlSitemapPair, ByUrl);
MAKESORTERTMPL(TProcessedUrlSitemapPair, BySitemap);
MAKESORTERTMPL(TProcessedUrlSitemapPair, CompareSM);

struct TUrlWithSitemapConfidence {
    ui32 HostId;
    ui64 UrlId;

    ui32 TruthfulCrawl;
    ui32 BaldLieCrawl;

    static const ui32 RecordSig = 0xB0B01945; // The battle of the Bulge

    TUrlWithSitemapConfidence()
    : HostId(0)
    , UrlId(0)
    , TruthfulCrawl(0)
    , BaldLieCrawl(0)
    { }

    static int ByUrl(const TUrlWithSitemapConfidence* rec1, const TUrlWithSitemapConfidence* rec2) {
        return CompareUids(rec1, rec2);
    }
};

MAKESORTERTMPL(TUrlWithSitemapConfidence, ByUrl);

struct THostStatistic {
    ui32 HostId;

    ui32 IndexedUrls;
    ui32 Language;

    static const ui32 RecordSig = 0xB0A01836; // The battle of the Alamo

    THostStatistic()
    : HostId(0)
    , IndexedUrls(0)
    , Language(0)
    { }

    static int ByHost(const THostStatistic* rec1, const THostStatistic* rec2) {
        return rec1->HostId - rec2->HostId;
    }
};

MAKESORTERTMPL(THostStatistic, ByHost);

struct TResettleDstRec {
    ui32 HostId;
    ui32 Time;

    static const ui32 RecordSig = 0xB0B01812; // The battle of Borodino
};

struct TSitemapExportRec {
    ui32 HostId;
    ui32 HostRank;
    ui64 UrlId;
    double UrlRank;
    url_t Name;

    static const ui32 RecordSig = 0xB0F00480; // The battle of Thermopylae

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByRanks(const TSitemapExportRec* a, const TSitemapExportRec* b) {
        int ret = -::compare(a->HostRank, b->HostRank);
        return ret ? ret : -(::compare(a->UrlRank, b->UrlRank));
    }
};

MAKESORTERTMPL(TSitemapExportRec, ByRanks);

struct TRankFactor {
    ui32 PrimaryKey; // HostId or DocId: TODO: change to union and fix datawork
    ui64 SecondaryKey; // UrlId

    ui32 Date;
    ui32 FactorId;
    ui32 Mask;

    float Value;

    static const ui32 RecordSig = 0xB0A01862; // The battle of Berlin

    TRankFactor()
    : PrimaryKey(0)
    , SecondaryKey(0)
    , Date(0)
    , FactorId(0)
    , Mask(0)
    , Value(0)
    { }
};

struct TMonitoringStatistic {
    static const size_t IntervalsNumber = 100;

    ui32 Date;
    ui32 StatType;
    ui32 FactorId;

    ui32 Items;
    float MinValue;
    float MaxValue;
    float AvgValue;
    float AvgSqrValue;

    float Distribution[IntervalsNumber];

    static const ui32 RecordSig = 0xB0C01914; // The battle of Cer

    TMonitoringStatistic()
    : Date(0)
    , StatType(0)
    , FactorId(0)
    , Items(0)
    , MinValue(0)
    , MaxValue(0)
    , AvgValue(0)
    , AvgSqrValue(0)
    {
        memset(Distribution, 0, sizeof(Distribution));
    }

    static int ByTypeFactor(const TMonitoringStatistic* rec1, const TMonitoringStatistic* rec2) {
        int ret = rec1->StatType - rec2->StatType;
        return ret ? ret : rec1->FactorId - rec2->FactorId;
    }

    static int ByDay(const TMonitoringStatistic* rec1, const TMonitoringStatistic* rec2) {
        int ret = rec2->Date - rec1->Date;
        return ret ? ret : ByTypeFactor(rec1, rec2);
    }
};

MAKESORTERTMPL(TMonitoringStatistic, ByTypeFactor);
MAKESORTERTMPL(TMonitoringStatistic, ByDay);

struct THostSigDoc {
    ui32 HostId;
    ui32 SigDocs;
    ui32 SigDocsSemidup;

    static const ui32 RecordSig = 0x02051988;

    operator hostid_t() const {
        return HostId;
    }

    static int ByHost(const THostSigDoc* rec1, const THostSigDoc* rec2) {
        return ::compare(rec1->HostId, rec2->HostId);
    }
};

MAKESORTERTMPL(THostSigDoc, ByHost);

struct TTreeNodeRec {
    ui32 HostId;
    ui32 NodeId;
    ui32 ParentId;
    ui32 Flags;
    ui32 NumOfDocs;
    ui32 NumOfDocsOnSearch;
    url_t Name;

    static const ui32 RecordSig = 0x11031986;

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByHostNode(const TTreeNodeRec* rec1, const TTreeNodeRec* rec2) {
        int ret = ::compare(rec1->HostId, rec2->HostId);
        return ret ? ret : ::compare(rec1->NodeId, rec2->NodeId);
    }

    operator hostid_t() const {
        return HostId;
    }
};

MAKESORTERTMPL(TTreeNodeRec, ByHostNode);

struct TUidAge {
    ui64 UrlId;
    ui32 HostId;
    ui32 Age;

    static const ui32 RecordSig = 0x43835FA1;

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TUidAge* a, const TUidAge* b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TUidAge, ByUid);

struct TUidLastAccessRec {
    ui64 UrlId;
    ui32 HostId;
    ui32 FileId;
    ui32 LastAccess;
    ui32 RecNum;

    static const ui32 RecordSig = 0x43835FA8;

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUidLastAccess(const TUidLastAccessRec* a, const TUidLastAccessRec* b) {
        int ret = CompareUids(a, b);
        if (ret)
            return ret;
        ret = ::compare(b->LastAccess, a->LastAccess);
        if (ret)
            return ret;
        return ::compare(a->RecNum, b->RecNum);
    }

    static int ByUidFileIdLastAccessRecNum(const TUidLastAccessRec* a, const TUidLastAccessRec* b) {
        int ret = CompareUids(a, b);
        if (ret)
            return ret;
        ret = ::compare(a->FileId, b->FileId);
        if (ret)
            return ret;
        ret = ::compare(b->LastAccess, a->LastAccess);
        if (ret)
            return ret;
        return ::compare(a->RecNum, b->RecNum);
    }


    static int ByRecNum(const TUidLastAccessRec* a, const TUidLastAccessRec* b) {
        return ::compare(a->RecNum, b->RecNum);
    }

};

MAKESORTERTMPL(TUidLastAccessRec, ByUidLastAccess);
MAKESORTERTMPL(TUidLastAccessRec, ByUidFileIdLastAccessRecNum);
MAKESORTERTMPL(TUidLastAccessRec, ByRecNum);

struct TUidLastAccessDocRec : public TUidLastAccessRec {
    ui64 Crc;
    ui32 SrcHostId;
    ui32 DocId;
    ui32 SourceId;

    static const ui32 RecordSig = 0x43835FA9;
};

MAKESORTERTMPL(TUidLastAccessDocRec, ByUidLastAccess);
MAKESORTERTMPL(TUidLastAccessDocRec, ByUidFileIdLastAccessRecNum);
MAKESORTERTMPL(TUidLastAccessDocRec, ByRecNum);

struct TDocLSourceCountRec {
    ui8 Source;
    ui8 Dummy1; ui8 Dummy2; ui8 Dummy3; // Needed due to the 32-bit field alignment
    ui32 Count;
    time_t Time;

    static const ui32 RecordSig = 0x43835FA2;
    TDocLSourceCountRec(ui8 source, ui32 count, time_t time)
    : Source(source)
    , Dummy1(0)
    , Dummy2(0)
    , Dummy3(0)
    , Count(count)
    , Time(time) { }

    static int BySourceTime(const TDocLSourceCountRec* a, const TDocLSourceCountRec* b) {
        return a->Source == b->Source ? a->Time > b->Time : a->Source < b->Source;
    }
};

struct TMultilangLinkRec {
    ui32 HostId;
    ui32 MultilangHostId;
    ui32 LangRegion;

    TMultilangLinkRec(ui32 hostId, ui32 multilangHostId, ui8 langRegion)
    : HostId(hostId)
    , MultilangHostId(multilangHostId)
    , LangRegion(langRegion)
    { }

    static const ui32 RecordSig = 0x43835FA3;

    static int ByHostPair(const TMultilangLinkRec* a, const TMultilangLinkRec* b) {
        return a->HostId == b->HostId ? ::compare(a->MultilangHostId, b->MultilangHostId) :
                ::compare(a->HostId, b->HostId);
    }
};

MAKESORTERTMPL(TMultilangLinkRec, ByHostPair);

// TODO remove me from here and dbview
struct THostHistoryRec {
    static const ui32 RecordSig = 0x2a71364a;
    ui32 HostId;
    TDataworkDate MergeDate;

    ui32 TotalDocs;
    ui32 RusSearch;
    ui32 RusMusorSearch;
    ui32 EngSearch;
    ui32 EngMusorSearch;
    ui32 UkrSearch;
    ui32 GeoSearch;
    ui32 TurSearch;
    ui32 GoldenWebSearch;
    ui32 RrgSearch;
    ui32 RusPlatinumSearch;
    ui32 AllLastSearch;

    operator hostid_t() const {
        return hostid_t(HostId);
    }

    static int ByHostDate(const THostHistoryRec* a, const THostHistoryRec* b) {
        int ret = compare(a->HostId, b->HostId);
        ret = ret ? ret : compare(a->MergeDate, b->MergeDate);
        return ret;
    }
};

MAKESORTERTMPL(THostHistoryRec, ByHostDate);

struct THostDocRec {
    static const ui32 RecordSig = 0xf06d974c;
    ui32 HostId;
    ui32 DocId;

    static int ByHostDoc(const THostDocRec* a, const THostDocRec* b) {
        int ret = compare(a->HostId, b->HostId);
        ret = ret ? ret : compare(a->DocId, b->DocId);
        return ret;
    }
    static int ByDoc(const THostDocRec* a, const THostDocRec* b) {
        return compare(a->DocId, b->DocId);
    }
};

MAKESORTERTMPL(THostDocRec, ByHostDoc);
MAKESORTERTMPL(THostDocRec, ByDoc);

struct TDocPruningRec {
    static const ui32 RecordSig = 0x2474c6fb;
    ui32 DocId;
    float Pruning;

    static int ByDoc(const TDocPruningRec* a, const TDocPruningRec* b) {
        return ::compare(a->DocId, b->DocId);
    }
};

MAKESORTERTMPL(TDocPruningRec, ByDoc);

struct TZippedPruningRec {
    static const ui32 RecordSig = 0x493e13ae;
    float Pruning;
};

struct THostSRStatRec {
    static const ui32 RecordSig = 0xf8b5457b;
    ui32 HostId;
    ui32 SRDeletedUrls;
    ui32 DeletedUrls;
    ui32 NewUrls;
};

struct THostSRDelRec {
    static const ui32 RecordSig = 0xf8b5457c;
    ui32 HostId;
    TDataworkDate MergeDate;

    ui32 SRDeletedDocs;
    ui32 SRDeletedUrls;
    ui32 DeletedUrls;
    ui32 NewUrls;

    operator hostid_t() const {
        return hostid_t(HostId);
    }

    static int ByHostDate(const THostSRDelRec* a, const THostSRDelRec* b) {
        int ret = compare(a->HostId, b->HostId);
        ret = ret ? ret : compare(a->MergeDate, b->MergeDate);
        return ret;
    }
};

MAKESORTERTMPL(THostSRDelRec, ByHostDate);

struct TUidSourceRecOld {
    static const ui32 RecordSig = 0xf8c5896e;

    ui32 HostId;
    ui64 UrlId;
    ui32 LastAccess;
    ui32 SourceId;

    TUidSourceRecOld(){}

    TUidSourceRecOld(ui32 hostId, ui64 urlId, ui32 lastAccess, ui32 sourceId)
        : HostId(hostId)
        , UrlId(urlId)
        , LastAccess(lastAccess)
        , SourceId(sourceId)
    {
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUidLastAccess(const TUidSourceRecOld* lhs, const TUidSourceRecOld* rhs) {
        int ret = CompareUids(lhs, rhs);
        ret = ret ? ret : -compare(lhs->LastAccess, rhs->LastAccess);
        return ret ? ret : compare(lhs->SourceId, rhs->SourceId);
    }
};

struct TUidSourceRec {
    static const ui32 RecordSig = 0xf8c5896f;

    ui32 HostId;
    ui64 UrlId;
    ui32 LastAccess;
    ui32 SourceId;
    ui32 CountryTimeStamp;
    ui32 CountryId;

    TUidSourceRec(){}

    TUidSourceRec(ui32 hostId, ui64 urlId, ui32 lastAccess, ui32 sourceId, ui32 countryTimeStamp, ui32 countryId)
        : HostId(hostId)
        , UrlId(urlId)
        , LastAccess(lastAccess)
        , SourceId(sourceId)
        , CountryTimeStamp(countryTimeStamp)
        , CountryId(countryId)
    {
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUidLastAccess(const TUidSourceRec* lhs, const TUidSourceRec* rhs) {
        int ret = CompareUids(lhs, rhs);
        ret = ret ? ret : -compare(lhs->LastAccess, rhs->LastAccess);
        return ret ? ret : compare(lhs->SourceId, rhs->SourceId);
    }
};

MAKESORTERTMPL(TUidSourceRec, ByUidLastAccess);

struct TUidPolicyIndexRec {
    static const ui32 RecordSig = 0xc47cb013;

    ui64 UrlId;
    ui32 HostId;
    ui32 Policy;
    ui32 Index;
    ui8  RobotZoneCode;

    TUidPolicyIndexRec(){}

    TUidPolicyIndexRec(ui32 hostId, ui64 urlId, ui32 policy, ui8 robotZoneCode, ui32 index)
        : UrlId(urlId)
        , HostId(hostId)
        , Policy(policy)
        , Index(index)
        , RobotZoneCode(robotZoneCode)
        {
        }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUidZonePolicy(const TUidPolicyIndexRec* a, const TUidPolicyIndexRec* b) {
        int ret = CompareUids(a, b);
        ret = ret ? ret : compare(a->RobotZoneCode, b->RobotZoneCode);
        ret = ret ? ret : compare(a->Policy, b->Policy);
        return ret;
    }
};

MAKESORTERTMPL(TUidPolicyIndexRec, ByUidZonePolicy);

struct TUidPolicyIndexRecUnstable {
    static const ui32 RecordSig = 0xc47cb012;

    ui64 UrlId;
    ui32 HostId;
    ui32 Policy;
    ui8  RobotZoneCode;
    ui8 Dummy1; ui8 Dummy2; ui8 Dummy3; // Needed due to the 32-bit field alignment
    ui32 Index;

    TUidPolicyIndexRecUnstable(){}

    TUidPolicyIndexRecUnstable(ui32 hostId, ui64 urlId, ui32 policy, ui8 robotZoneCode, ui32 index)
        : UrlId(urlId)
        , HostId(hostId)
        , Policy(policy)
        , RobotZoneCode(robotZoneCode)
        , Index(index)
        {
        }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TUidPolicyIndexRecUnstable* a, const TUidPolicyIndexRecUnstable* b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TUidPolicyIndexRecUnstable, ByUid);

struct TUidPolicyRec {
    ui64 UrlId;
    ui32 HostId;
    ui32 Policy;

    static const ui32 RecordSig = 0x9ca8b027;

    TUidPolicyRec(ui32 hostId, ui64 urlId, ui32 policy)
        : UrlId(urlId)
        , HostId(hostId)
        , Policy(policy)
    {}

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TUidPolicyRec *a, const TUidPolicyRec *b) {
        return ::CompareUids(a, b);
    }
};

MAKESORTERTMPL(TUidPolicyRec, ByUid);

struct THostPenaltyDeltaRec {
    ui32 HostId;
    ui32 Delta;
    ui32 LastAccess;

    static const ui32 RecordSig = 0x9ca8b227;
    static int ByHostId(const THostPenaltyDeltaRec* a, const THostPenaltyDeltaRec* b) {
        return compare(a->HostId, b->HostId);
    }

    static int ByHostIdLastAccessDesc(const THostPenaltyDeltaRec* a, const THostPenaltyDeltaRec* b) {
        int cmp = ::compare(a->HostId, b->HostId);
        if (cmp) {
            return cmp;
        }
        return ::compare(b->LastAccess, a->LastAccess);
    }
};

MAKESORTERTMPL(THostPenaltyDeltaRec, ByHostId);
MAKESORTERTMPL(THostPenaltyDeltaRec, ByHostIdLastAccessDesc);

struct TUidUploadRuleRec {
    static const ui32 RecordSig = 0xd74db012;

    ui64 UrlId;
    ui32 HostId;
    ui32 UploadRuleId;

    TUidUploadRuleRec(){}

    TUidUploadRuleRec(ui32 hostId, ui64 urlId, ui32 uploadRuleId)
        : UrlId(urlId)
        , HostId(hostId)
        , UploadRuleId(uploadRuleId)
        {
        }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TUidUploadRuleRec* a, const TUidUploadRuleRec* b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TUidUploadRuleRec, ByUid);

struct TUidClicksRec {
    static const ui32 RecordSig = 0xd74db013;

    ui64 UrlId;
    ui32 HostId;
    float Clicks;

    TUidClicksRec(){}

    TUidClicksRec(ui32 hostId, ui64 urlId, float clicks)
        : UrlId(urlId)
        , HostId(hostId)
        , Clicks(clicks)
        {
        }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TUidClicksRec* a, const TUidClicksRec* b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TUidClicksRec, ByUid);

struct TUidClicksRankRec : public TUidClicksRec {
    static const ui32 RecordSig = 0xd74db014;

    ui32 Rank;

    TUidClicksRankRec(){}

    TUidClicksRankRec(ui32 hostId, ui64 urlId, float clicks, ui32 rank)
        : TUidClicksRec(hostId, urlId, clicks)
        , Rank(rank)
        {
        }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByHostIdRank(const TUidClicksRankRec* lhs, const TUidClicksRankRec* rhs) {
        int ret = ::compare(lhs->HostId, rhs->HostId);
        return ret ? ret : ByRank(lhs, rhs);
    }

    static int ByRank(const TUidClicksRankRec* lhs, const TUidClicksRankRec* rhs) {
        return ::compare(lhs->Rank, rhs->Rank);
    }
};

MAKESORTERTMPL(TUidClicksRankRec, ByHostIdRank);

struct THostQuotaRec {
    static const ui32 RecordSig = 0xd74db015;

    ui32 HostId;
    ui32 Quota;

    THostQuotaRec(){}

    THostQuotaRec(ui32 hostId, ui32 quota)
        : HostId(hostId)
        , Quota(quota)
        {
        }
};

struct TNeed4Stats {
    static const ui32 RecordSig = 0x1e4542a1;

    ui32 Need4Stats;

    TNeed4Stats()
    {
    }

    TNeed4Stats(bool need4Stats)
    {
        Need4Stats = need4Stats ? 1 : 0;
    }
};

struct TUidRankZoneRecExt: public TNeed4Stats, public TUidRankZone {
    static const ui32 RecordSig = 0x1e4542ae;

    TUidRankZoneRecExt(const TUrlRecNoProto* rec, ui8 zone, double rank, bool /*isnew = false*/, ui8 /*clientType = 0*/, bool need4Stats = 0)
        : TNeed4Stats(need4Stats)
        , TUidRankZone(rec->HostId, rec->UrlId, rank, zone)
    {
    }

    TUidRankZoneRecExt(const TUidRankZoneRecExt* rec, bool need4Stats)
        : TNeed4Stats(need4Stats)
        , TUidRankZone(rec)
    {
    }

    size_t SizeOf() const {
        return 25;
    }
};

MAKESORTERTMPL(TUidRankZoneRecExt, ByRank);

struct TPreQueueRecExt: public TNeed4Stats, public TPreQueue {
    static const ui32 RecordSig = 0xcee7fd13;

    TPreQueueRecExt(const TUrlRecNoProto* url, ui8 zone, double rank, bool isNewPolicy, ui8 clientType = 0,
            ui16 langRegion = GetDefaultLangRegion(), ui16 needToUpdateLangRegion = 0, bool need4Stats = 0)
        : TNeed4Stats(need4Stats)
        , TPreQueue(url, zone, rank, isNewPolicy, clientType, langRegion, needToUpdateLangRegion)
    {
    }

    TPreQueueRecExt(const TPreQueueRecExt* rec, bool need4Stats)
        : TNeed4Stats(need4Stats)
        , TPreQueue(rec)
    {
    }

    size_t SizeOf() const {
        return offsetof(TPreQueueRecExt, Name) + strlen(Name) + 1;
    }
};

MAKESORTERTMPL(TPreQueueRecExt, ByRank);

struct TMovedMirrorDocInfoRec {

    ui64 TextCRC;
    i32  HttpModTime;
    ui32 LastAccess;
    ui32 LangRegion;
    ui32 HttpCode;
    ui32 SourceId;
    ui32 Flags;
    ui32 DocId;

    static const ui32 RecordSig = 0xcee7fd14;

    TMovedMirrorDocInfoRec(
        const ui64 textCRC,
        const i32 httpModTime,
        const ui32 lastAccess,
        const ui32 langRegion,
        const ui32 httpCode,
        const ui32 sourceId,
        const ui32 flags,
        const ui32 docId)
        : TextCRC(textCRC)
        , HttpModTime(httpModTime)
        , LastAccess(lastAccess)
        , LangRegion(langRegion)
        , HttpCode(httpCode)
        , SourceId(sourceId)
        , Flags(flags)
        , DocId(docId)
    {
    }
};

struct TUidLangRegion {
    ui64 UrlId;
    ui32 HostId;
    dbscheeme::host_t LangRegion;

    size_t SizeOf() const {
        return offsetofthis(LangRegion) + strlen(LangRegion) + 1;
    }

    static const ui32 RecordSig = 0x5499BF49;

    static int ByUid(const TUidLangRegion* a, const TUidLangRegion* b) {
        return CompareUids(a, b);
    }
};

MAKESORTERTMPL(TUidLangRegion, ByUid);

struct TUidSnipRec {
    ui64 UrlId;
    ui32 HostId;
    i32 Time;
    blobtext_t Data;

    static const ui32 RecordSig = 0x5499C000;

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }
    size_t SizeOf() const {
        return offsetof(TUidSnipRec, Data) + strlen(Data) + 1;
    }
    static int ByUid(const TUidSnipRec* a, const TUidSnipRec* b) {
        return CompareUids(a, b);
    }
    static int ByUidDescTime(const TUidSnipRec* a, const TUidSnipRec* b) {
        int ret = CompareUids(a, b);
        return ret ? ret : ::compare(b->Time, a->Time);
    }
};

MAKESORTERTMPL(TUidSnipRec, ByUid);
MAKESORTERTMPL(TUidSnipRec, ByUidDescTime);

class TUrlGroup {
public:
    ui32 Group;
    ui32 ModTime;
    ui64 UrlId;
    ui32 HostId;
    ui32 DocId;

public:
    bool operator<(const TUrlGroup& ug) const {
        int res = ::compare(Group, ug.Group);
        if (res)
            return res < 0;
        res = ::compare(UrlId, ug.UrlId);
        if (res)
            return res < 0;
        return ug.ModTime < ModTime;
    }
};

class TUrlGroup1 : public TUrlGroup {
public:
    bool operator<(const TUrlGroup1& ug) const {
        int res = ::compare(HostId, ug.HostId);
        if (res)
            return res < 0;
        return UrlId < ug.UrlId;
    }
};

#pragma pack(pop)

#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic pop
#endif
