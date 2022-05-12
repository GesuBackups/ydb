#pragma once

#include <yweb/protos/robot.pb.h>

#include <time.h>
#include "dbtypes.h"
#include <library/cpp/microbdb/sorterdef.h>
#include "extlinkflags.h"

#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#pragma pack(push, 4)

// convert time_t to ui16
inline ui16 TimeToDate(time_t tim) {
    return (ui16)((ui32)tim / (3600u * 24u));
}

inline time_t DateToTime(ui16 date) {
    return (time_t)(date * 3600u * 24u);
}

static const ui16 SYNC_INTERVAL_DAYS = 8;
static const ui16 SYNC_GRACE_DAYS = 3;

inline bool OutOfSync(ui16 synced, ui16 now, ui16 grace = 0) {
    if (synced > now)
        return false;
    return ((now - synced) > (SYNC_INTERVAL_DAYS + grace));
}

// records in temporary files during merge

struct TLinkIntRec {
    ui64        UrlIdTo;
    ui64        Fnv;
    ui64        UrlIdFrom;
    ui32        HostId;
    ui16        DateDiscovered;
    ui16        Flags;

    TLinkIntRec()
        : UrlIdTo(0)
        , Fnv(0)
        , UrlIdFrom(0)
        , HostId(0)
        , DateDiscovered(0)
        , Flags(0)
    {}

    static const ui32 RecordSig = 0x21088370;

    static int ByDstFnvSrc(const TLinkIntRec *a, const TLinkIntRec *b) {
        int ret = compare(a->HostId, b->HostId);
        ret = ret ? ret : compare(a->UrlIdTo, b->UrlIdTo);
        ret = ret ? ret : compare(a->Fnv, b->Fnv);
        return ret ? ret : compare(a->UrlIdFrom, b->UrlIdFrom);
    }

    static int ByDstFnv(const TLinkIntRec *a, const TLinkIntRec *b) {
        int ret = compare(a->HostId, b->HostId);
        ret = ret ? ret : compare(a->UrlIdTo, b->UrlIdTo);
        return ret ? ret : compare(a->Fnv, b->Fnv);
    }

    static int BySrcDstFnv(const TLinkIntRec *a, const TLinkIntRec *b) {
        int ret = compare(a->HostId, b->HostId);
        ret = ret ? ret : compare(a->UrlIdFrom, b->UrlIdFrom);
        ret = ret ? ret : compare(a->UrlIdTo, b->UrlIdTo);
        return ret ? ret : compare(a->Fnv, b->Fnv);
    }

    static int ByUrlIdFrom(const TLinkIntRec *a, const TLinkIntRec *b) {
        return compare(a->UrlIdFrom, b->UrlIdFrom);
    }

    inline ui32 GetHostIdFrom() const noexcept {
        return HostId;
    }

    inline ui32 GetHostIdTo() const noexcept {
        return HostId;
    }

    inline ui32 GetSeoFlags() const noexcept {
        return 0;
    }

    /// For compatibility with TLinkExtRec
    inline bool IsRedirect() const noexcept {
        return false;
    }

    void SetHostIdFrom(ui32 hostId) {
        HostId = hostId;
    }

    void SetHostIdTo(ui32 hostId) {
        HostId = hostId;
    }
};

MAKESORTERTMPL(TLinkIntRec, ByDstFnvSrc);
MAKESORTERTMPL(TLinkIntRec, ByDstFnv);
MAKESORTERTMPL(TLinkIntRec, BySrcDstFnv);
MAKESORTERTMPL(TLinkIntRec, ByUrlIdFrom);

struct TLinkIntRecSieve {
    static int Sieve(TLinkIntRec *prev, const TLinkIntRec *next) {
        if (TLinkIntRec::ByDstFnvSrc(prev, next))
            return 0;
        else
            return 1;
    }
};

template<template<typename T> class TSieveComparer>
struct TLinkIntRecSieveTemplated {
    typedef TSieveComparer<TLinkIntRec> sieve_comparer_type;
    static int Sieve(TLinkIntRec *prev, const TLinkIntRec *next) {
        if (sieve_comparer_type()(prev, next))
            return 0;
        else
            return 1;
    }
};

struct TLinkExtRec {
    ui64        UrlIdFrom;
    ui64        UrlIdTo;
    ui64        Fnv;
    ui32        HostIdFrom;
    ui32        HostIdTo;
    ui32        Flags;
    ui16        DateDiscovered;
    ui16        DateSynced;
    ui32        SeoFlags;

    TLinkExtRec()
        : UrlIdFrom(0)
        , UrlIdTo(0)
        , Fnv(0)
        , HostIdFrom(0)
        , HostIdTo(0)
        , Flags(0)
        , DateDiscovered(0)
        , DateSynced(0)
        , SeoFlags(0)
    {}

    static const ui32 RecordSig = 0x21088396;

    static int BySrcDstFnv(const TLinkExtRec *a, const TLinkExtRec *b) {
        int ret = compare(a->HostIdFrom, b->HostIdFrom);
        if (ret)
            return ret;
        ret = compare(a->UrlIdFrom, b->UrlIdFrom);
        if (ret)
            return ret;
        ret = compare(a->HostIdTo, b->HostIdTo);
        if (ret)
            return ret;
        ret = compare(a->UrlIdTo, b->UrlIdTo);
        if (ret)
            return ret;
        return compare(a->Fnv, b->Fnv);
    }

    static int BySrcDstFnv2(const TLinkExtRec *a, const TLinkExtRec *b) {
        int ret = compare(a->HostIdFrom & 0x7FFFFFFF, b->HostIdFrom & 0x7FFFFFFF);
        if (ret)
            return ret;
        ret = compare(a->UrlIdFrom, b->UrlIdFrom);
        if (ret)
            return ret;
        ret = compare(a->HostIdTo, b->HostIdTo);
        if (ret)
            return ret;
        ret = compare(a->UrlIdTo, b->UrlIdTo);
        if (ret)
            return ret;
        ret = compare(a->Fnv, b->Fnv);
        if (ret)
            return ret;
        ret = compare(a->DateDiscovered, b->DateDiscovered);
        if (ret)
            return ret;
        ret = compare(a->Flags, b->Flags);
        if (ret)
            return ret;
        return compare(a->SeoFlags, b->SeoFlags);
    }

    static int ByDstFnvSrc(const TLinkExtRec *a, const TLinkExtRec *b) {
        int ret = compare(a->HostIdTo, b->HostIdTo);
        if (ret)
            return ret;
        ret = compare(a->UrlIdTo, b->UrlIdTo);
        if (ret)
            return ret;
        ret = compare(a->Fnv, b->Fnv);
        if (ret)
            return ret;
        ret = compare(a->HostIdFrom, b->HostIdFrom);
        if (ret)
            return ret;
        return compare(a->UrlIdFrom, b->UrlIdFrom);
    }

    static int ByUrlIdFrom(const TLinkExtRec *a, const TLinkExtRec *b) {
        return compare(a->UrlIdFrom, b->UrlIdFrom);
    }

    inline ui32 GetHostIdFrom() const noexcept {
        return HostIdFrom;
    }

    void SetHostIdFrom(ui32 hostId) {
        HostIdFrom = hostId;
    }

    inline ui32 GetHostIdTo() const noexcept {
        return HostIdTo;
    }

    void SetHostIdTo(ui32 hostId) {
        HostIdTo = hostId;
    }

    inline ui32 GetSeoFlags() const noexcept {
        return SeoFlags;
    }

    ui16 Segment() const {
        return (ui16)TLinkFlagSegment::Unpack(Flags);
    }

    ui16 Offtop() const {
        return (ui16)TLinkFlagOfftop::Unpack(Flags);
    }

    ui16 SeoMark() const {
        return (ui16)TLinkFlagSeoMark::Unpack(Flags);
    }

    ui16 SeoDoc() const {
        return (ui16)TLinkFlagSeoDoc::Unpack(Flags);
    }

    ui16 SeoSubgr() const {
        return (ui16)TLinkFlagSeoSubgr::Unpack(Flags);
    }

    ui16 SeoPrice() const {
        return (ui16)TLinkFlagSeoPrice::Unpack(Flags);
    }

    ui16 DocFoldingLevel() const {
        return (ui16)TLinkFlagDocFoldingLevel::Unpack(Flags);
    }

    ui16 SeoTrash() const {
        return (ui16)TLinkFlagSeoTrash::Unpack(Flags);
    }
    ui32 TextNorm() const {
        return TLinkTFlagTextNorm::Unpack(SeoFlags);
    }
    ui32 TextSale() const {
        return TLinkTFlagTextSale::Unpack(SeoFlags);
    }
    ui32 TextThemeSt() const {
        return TLinkTFlagTextThemeSt::Unpack(SeoFlags);
    }
    ui32 TextThemeId() const {
        return TLinkTFlagTextThemeId::Unpack(SeoFlags);
    }

    ui16 Age() const {
#ifdef TEST_MERGE
        return (ui16)(0x7FFFFFFF / (60 * 60 * 24)) - DateDiscovered;
#else
        static const ui16 daysToNow = static_cast<ui16>((ui64)time(nullptr) / (60 * 60 * 24));
        return daysToNow - DateDiscovered;
#endif
    }

    inline bool IsRedirect() const noexcept {
        return TLinkTFlagExtRedirect::Unpack(SeoFlags);
    }
    bool IsSiteWideLink() const {
        return TLinkTFlagIsSiteWideLink::Unpack(SeoFlags);
    }
    ELinkTarget Target() const {
        return static_cast<ELinkTarget>(TLinkTFlagLinkTargetAttr::Unpack(SeoFlags));
    }
};

MAKESORTERTMPL(TLinkExtRec, BySrcDstFnv);
MAKESORTERTMPL(TLinkExtRec, BySrcDstFnv2);
MAKESORTERTMPL(TLinkExtRec, ByDstFnvSrc);
MAKESORTERTMPL(TLinkExtRec, ByUrlIdFrom);

struct TLinkExtRecSieve {
    static int Sieve(TLinkExtRec *prev, const TLinkExtRec *next) {
        if (TLinkExtRec::BySrcDstFnv(prev, next))
            return 0;
        else
            return 1;
    }
};

struct TLinkExtNameRec : public TLinkExtRec {
public:
    url_t Name;

    static const ui32 RecordSig = 0x21088394;

public:
    size_t SizeOf() const {
        return offsetof(TLinkExtNameRec, Name) + strlen(Name) + 1;
    }
};

// incoming linkdb

struct TLinkIncRec : public TLinkExtRec {
    ui32        SrcSeg;

    TLinkIncRec()
        : SrcSeg(0)
    {}

    static const ui32 RecordSig = 0x21088398;

    static int ByDstFnvSrcSeg(const TLinkIncRec *a, const TLinkIncRec *b) {
        int ret = compare(a->HostIdTo, b->HostIdTo);
        ret = ret ? ret : compare(a->UrlIdTo, b->UrlIdTo);
        ret = ret ? ret : compare(a->Fnv, b->Fnv);
        ret = ret ? ret : compare(a->SrcSeg, b->SrcSeg);
        ret = ret ? ret : compare(a->HostIdFrom, b->HostIdFrom);
        return ret ? ret : compare(a->UrlIdFrom, b->UrlIdFrom);
    }

};

MAKESORTERTMPL(TLinkIncRec, BySrcDstFnv);
MAKESORTERTMPL(TLinkIncRec, ByDstFnvSrc);
MAKESORTERTMPL(TLinkIncRec, ByUrlIdFrom);
MAKESORTERTMPL(TLinkIncRec, ByDstFnvSrcSeg);

namespace UidActions {
    const ui16 ACTION_DELETE            = 0x1;
    const ui16 ACTION_ADD               = 0x2;
    const ui16 ACTION_CHANGE_SOURCE     = 0x4;
}

struct TUidActionRec {
    ui64        UrlId;
    ui64        RelUrlId;
    ui64        DateUrlId;
    ui32        HostId;
    ui32        Action;

    TUidActionRec()
        : UrlId(0)
        , RelUrlId(0)
        , DateUrlId(0)
        , HostId(0)
        , Action(0)
    {}

    static const ui32 RecordSig = 0x21088383;

    static int HostActUid(const TUidActionRec *a, const TUidActionRec *b) {
        int ret = compare(a->HostId, b->HostId);
        ret = ret ? ret : compare(a->Action, b->Action);
        return ret ? ret : compare(a->UrlId, b->UrlId);
    }
};

MAKESORTERTMPL(TUidActionRec, HostActUid);

struct TUidActionRecSieve {
    static int Sieve(TUidActionRec *prev, const TUidActionRec *next) {
        if (TUidActionRec::HostActUid(prev, next))
            return 0;
        else
            return 1;
    }
};

// records in permanent files before/after merge

struct TLinkIntHostRecOld {
    ui32        HostId;
    ui32        DstOffset;

    TLinkIntHostRecOld()
        : HostId(0)
        , DstOffset(0)
    {}

    static const ui32 RecordSig = 0x21088372;

    operator hostid_t() const {
        return HostId;
    }
};

struct TLinkIntHostRec {
    ui32        HostId;
    ui64        DstOffset;

    TLinkIntHostRec()
        : HostId(0)
        , DstOffset(0)
    {}

    static const ui32 RecordSig = 0x21088374;

    operator hostid_t() const {
        return HostId;
    }
};

struct TLinkIntDstFnvRecOld {
    ui64        UrlIdTo;
    ui64        Fnv;
    ui32        SrcOffset;

    TLinkIntDstFnvRecOld()
        : UrlIdTo(0)
        , Fnv(0)
        , SrcOffset(0)
    {}

    static const ui32 RecordSig = 0x21088373;
};

struct TLinkIntDstFnvRec {
    ui64        UrlIdTo;
    ui64        Fnv;
    ui64        SrcOffset;

    TLinkIntDstFnvRec()
        : UrlIdTo(0)
        , Fnv(0)
        , SrcOffset(0)
    {}

    static const ui32 RecordSig = 0xDD11DD22;
};

struct TLinkIntSrcRec {
    ui64        UrlIdFrom;
    ui16        DateDiscovered;
    ui16        Flags;

    TLinkIntSrcRec()
        : UrlIdFrom(0)
        , DateDiscovered(0)
        , Flags(0)
    {}

    static const ui32 RecordSig = 0x21088375;
};

struct TLinkExtSrcRecOld {
    ui64        UrlIdFrom;
    ui32        HostIdFrom;
    ui32        DstOffset;

    TLinkExtSrcRecOld()
        : UrlIdFrom(0)
        , HostIdFrom(0)
        , DstOffset(0)
    {}

    static const ui32 RecordSig = 0x21088380;

    operator urlid_t() const {
        return urlid_t(HostIdFrom, UrlIdFrom);
    }
};

struct TLinkExtSrcRec {
    ui64        UrlIdFrom;
    ui32        HostIdFrom;
    ui64        DstOffset;

    TLinkExtSrcRec()
        : UrlIdFrom(0)
        , HostIdFrom(0)
        , DstOffset(0)
    {}

    static const ui32 RecordSig = 0x21088381;

    operator urlid_t() const {
        return urlid_t(HostIdFrom, UrlIdFrom);
    }
};

struct TLinkExtDstRec {
    ui64        UrlIdTo;
    ui64        Fnv;
    ui32        HostIdTo;
    ui32        Flags;
    ui16        DateDiscovered;
    ui16        DateSynced;
    ui32        SeoFlags;

    TLinkExtDstRec()
        : UrlIdTo(0)
        , Fnv(0)
        , HostIdTo(0)
        , Flags(0)
        , DateDiscovered(0)
        , DateSynced(0)
        , SeoFlags(0)
    {}

    static const ui32 RecordSig = 0x21088397; //+SeoFlags
};

struct TLinkIncDstRec {
    ui64        UrlIdTo;
    ui32        HostIdTo;
    ui32        FnvOffset;

    TLinkIncDstRec()
        : UrlIdTo(0)
        , HostIdTo(0)
        , FnvOffset(0)
    {}

    static const ui32 RecordSig = 0x21088388;

    operator urlid_t() const {
        return urlid_t(HostIdTo, UrlIdTo);
    }
};

struct TLinkIncFnvRecOld {
    ui64        Fnv;
    ui32        SrcOffset;

    TLinkIncFnvRecOld()
        : Fnv(0)
        , SrcOffset(0)
    {}

    static const ui32 RecordSig = 0x21088389;
};

struct TLinkIncFnvRec {
    ui64        Fnv;
    ui64        SrcOffset;

    TLinkIncFnvRec()
        : Fnv(0)
        , SrcOffset(0)
    {}

    static const ui32 RecordSig = 0x21088387;
};

struct TLinkIncSrcRec {
    ui64        UrlIdFrom;
    ui32        HostIdFrom;
    ui16        SrcSeg;
    ui16        DateDiscovered;
    ui16        DateSynced;
    ui16        Flags;

    TLinkIncSrcRec()
        : UrlIdFrom(0)
        , HostIdFrom(0)
        , SrcSeg(0)
        , DateDiscovered(0)
        , DateSynced(0)
        , Flags(0)
    {}

    static const ui32 RecordSig = 0x21088390;
};

// text records
struct TRefTextRec {
    ui64        Fnv;
    ui32        RefCount;

    TRefTextRec()
        : Fnv(0)
        , RefCount(0)
    {}

    static const ui32 RecordSig = 0x21088384;

    static int ByFnv(const TRefTextRec *a, const TRefTextRec *b) {
        return compare(a->Fnv, b->Fnv);
    }
};

MAKESORTERTMPL(TRefTextRec, ByFnv);

struct TRefTextRecSieve {
    static int Sieve(TRefTextRec *prev, const TRefTextRec *next) {
        if (TRefTextRec::ByFnv(prev, next))
            return 0;
        else {
            // calculare ref count
            if (prev != next)
                prev->RefCount += next->RefCount;
            return 1;
        }
    }
};

// This is an auxiliary class for TLinkTextRec::Texts:
//  helps in serializing the field
//  (as it generates a real type and hence allows to overload ReadFiels and WriteField properly)
//  + contains some functionality
struct TLinkTexts {
private:
    linktext_t  Buffer;
    enum { FREE_TEXT_SIZE = sizeof(linktext_t) - 2 };

public:
    TLinkTexts() {
        Buffer[0] = 0;
        Buffer[1] = 0;
    }

    TLinkTexts(const TLinkTexts& other) {
        *this = other;
    }

    TLinkTexts& operator=(const TLinkTexts& other) {
        if (this != &other) {
            size_t length0 = strlen(other.Buffer);
            const char* extendedText = other.Buffer + length0 + 1;
            size_t length1 = strlen(extendedText);
            DoCopy(other.Buffer, length0, extendedText, length1);
        }
        return *this;
    }

    const char* GetText() const {
        return Buffer;
    }

    const char* GetExtendedText() const {
        return Buffer + strlen(Buffer) + 1;
    }

    void SetTexts(const char* text, size_t length0, const char* extendedText, size_t length1) {
        assert(text != NULL);
        length0 = Min(length0, Min(strlen(text), (size_t)FREE_TEXT_SIZE));
        length1 = (extendedText != nullptr)? Min(length1, Min(strlen(extendedText), (size_t)FREE_TEXT_SIZE - length0)) : 0;
        DoCopy(text, length0, extendedText, length1);
    }

    size_t SizeOf() const {
        const size_t length0 = strlen(Buffer) + 1;
        return offsetof(TLinkTexts, Buffer) + length0 + strlen(Buffer + length0) + 1;
    }

    void WriteTo(char*& ptr) const {
        size_t length0 = strlen(Buffer);
        memcpy(ptr, Buffer, length0);
        ptr += length0;
        *ptr = '\t';
        ++ptr;
        const char* extendedText = Buffer + length0 + 1;
        size_t length1 = strlen(extendedText);
        memcpy(ptr, extendedText, length1);
        ptr += length1;
    }

    bool ReadFrom(const char*& ptr) {
        const char* pend = nullptr;
        const char* text = ptr;
        const char* extendedText = nullptr;
        size_t length0 = 0, length1 = 0;
        if ((pend = strchr(text, '\t')) != nullptr) {
            length0 = pend - text;
            extendedText = pend + 1;
            if ((pend = strchr(extendedText, '\t')) != nullptr || (pend = strchr(extendedText, '\n')) != nullptr) {
                length1 = pend - extendedText;
            }
        } else if ((pend = strchr(text, '\n')) != nullptr) { // reading old format
            length0 = pend - text;
        } else {
            return false;
        }
        if (length0 > FREE_TEXT_SIZE && length1 == 0) {
            length0 = FREE_TEXT_SIZE;
        }
        if ((length0 + length1) > FREE_TEXT_SIZE) {
            return false;
        }
        DoCopy(text, length0, extendedText, length1);
        ptr = pend;
        return true;
    }

private:
    void DoCopy(const char* text, size_t length0, const char* extendedText, size_t length1) {
        strncpy(Buffer, text, length0);
        Buffer[length0] = 0;
        ++length0;
        if (extendedText != nullptr) {
            strncpy(Buffer + length0, extendedText, length1);
        } else {
            assert(length1 == 0);
        }
        Buffer[length0 + length1] = 0;
    }
};

struct TLinkTextRec : public TRefTextRec {
    TLinkTexts Texts; // contains two null terminated text entries.

    static const ui32 RecordSig = 0x4C545201; // "LTR\x01"

    void SetText(const char* text, size_t length) {
        Texts.SetTexts(text, length, nullptr, 0);
    }

    void SetTexts(const char* text, size_t length0, const char* extendedText, size_t length1) {
        Texts.SetTexts(text, length0, extendedText, length1);
    }

    void CopyTexts(const TLinkTextRec& record) {
        if (this != &record) {
            Texts = record.Texts;
        }
    }

    size_t SizeOf() const {
        return offsetof(TLinkTextRec, Texts) + Texts.SizeOf();
    }

    operator crc64_t() const {
        return Fnv;
    }

};

inline const char* GetText(const TLinkTextRec* record) {
    assert(record != NULL);
    return record->Texts.GetText();
}

inline const char* GetExtendedText(const TLinkTextRec* record) {
    assert(record != NULL);
    return record->Texts.GetExtendedText();
}

MAKESORTERTMPL(TLinkTextRec, ByFnv);

struct TOldLinkTextRec : public TRefTextRec {
    linktext_t  Text;

    static const ui32 RecordSig = 0x21088376;

    size_t SizeOf() const {
        return offsetof(TOldLinkTextRec, Text) + strlen(Text) + 1;
    }

    operator crc64_t() const {
        return Fnv;
    }
};
MAKESORTERTMPL(TOldLinkTextRec, ByFnv);

struct TRefUrlRec {
    ui64        UrlId;
    ui32        HostId;
    ui32        Flags;
    i32         RefCount;

    TRefUrlRec()
        : UrlId(0)
        , HostId(0)
        , Flags(0)
        , RefCount(0)
    {}

    static const ui32 RecordSig = 0x21088385;

    static int ByHostUid(const TRefUrlRec *a, const TRefUrlRec *b) {
        int ret = compare(a->HostId, b->HostId);
        return ret ? ret : compare(a->UrlId, b->UrlId);
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }
};

MAKESORTERTMPL(TRefUrlRec, ByHostUid);

struct TRefUrlRecSieve {
    static int Sieve(TRefUrlRec *prev, const TRefUrlRec *next) {
        if (TRefUrlRec::ByHostUid(prev, next))
            return 0;
        else {
            // calculare ref count
            if (prev != next)
                prev->RefCount += next->RefCount;
            return 1;
        }
    }
};

// foreign url and host records
struct TExtUrlRec : public TRefUrlRec {

    url_t       Name;

    TExtUrlRec() {
        Name[0] = 0;
    }

    static const ui32 RecordSig = 0x21088377;

    static size_t OffsetOfName() {
        return offsetof(TExtUrlRec, Name);
    }

    size_t SizeOf() const {
        return OffsetOfName() + strlen(Name) + 1;
    }
};

MAKESORTERTMPL(TExtUrlRec, ByHostUid);

struct TExtHostRec {
    ui32        HostId;
    ui16        DstSeg;
    dbscheeme::host_t      Name;

    TExtHostRec()
        : HostId(0)
        , DstSeg(0)
    {
        Name[0] = 0;
    }

    static const ui32 RecordSig = 0x21088378;

    static size_t OffsetOfName() {
        static TExtHostRec rec;
        return rec.OffsetOfNameImpl();
    }

    size_t SizeOf() const {
        return offsetof(TExtHostRec, Name) + strlen(Name) + 1;
    }

    static int ById(const TExtHostRec *a, const TExtHostRec *b) {
        return compare(a->HostId, b->HostId);
    }

    static int ByName(const TExtHostRec *a, const TExtHostRec *b) {
        return strcmp(a->Name, b->Name);
    }

    static bool IsForeign(ui32 hostId) {
        return (hostId >> 30) == 2;
    }
    operator hostid_t() const {
        return HostId;
    }

private:
    size_t OffsetOfNameImpl() const {
        return offsetofthis(Name);
    }
};

MAKESORTERTMPL(TExtHostRec, ById);
MAKESORTERTMPL(TExtHostRec, ByName);

struct TExtHostRecSieve {
    static int Sieve(TExtHostRec *prev, const TExtHostRec *next) {
        if (TExtHostRec::ById(prev, next))
            return 0;
        else
            return 1;
    }
};

struct TGroupHostRec {
    ui32        HostId;
    ui32        OwnerId;

    TGroupHostRec()
        : HostId(0)
        , OwnerId(0)
    {}

    static const ui32 RecordSig = 0x21088393;

    size_t SizeOf() const {
        return sizeof(TGroupHostRec);
    }
};

struct TLinkExtSaveDateRec {
    ui64        UrlIdTo;
    ui64        Fnv;
    ui32        DocId;
    ui16        DateDiscovered;
    dbscheeme::host_t      HostNameTo;

    static const ui32 RecordSig = 0x21088395;

    TLinkExtSaveDateRec(ui64 fnv, ui64 urlIdTo, const char *hostNameTo)
        : UrlIdTo(urlIdTo)
        , Fnv(fnv)
        , DocId(0)
        , DateDiscovered(0)
    {
        strcpy(HostNameTo, hostNameTo);
    }

    TLinkExtSaveDateRec() {
        TLinkExtSaveDateRec(0, 0, "");
    }

    size_t SizeOf() const {
        return offsetof(TLinkExtSaveDateRec, HostNameTo) + strlen(HostNameTo) + 1;
    }

    static int ByDocId(const TLinkExtSaveDateRec *a, const TLinkExtSaveDateRec *b) {
        return compare(a->DocId, b->DocId);
    }

    static int ByFnvTo(const TLinkExtSaveDateRec *a, const TLinkExtSaveDateRec *b) {
        int ret = compare(a->Fnv, b->Fnv);
        if (ret)
            return ret;
        ret = compare(a->UrlIdTo, b->UrlIdTo);
        return ret ? ret : stricmp(a->HostNameTo, b->HostNameTo);
    }
};


MAKESORTERTMPL(TLinkExtSaveDateRec, ByDocId);
MAKESORTERTMPL(TLinkExtSaveDateRec, ByFnvTo);

const size_t EXT_LINK_DATES_BUFFER_SIZE =  1 << 20;

struct TLinkWithTextRec {
    ui64    Fnv;
    ui64    UrlId;
    ui64    UrlIdTo;
    ui32    HostId;
    ui32    HostIdTo;
    TLinkTexts Texts;

    static const ui32 RecordSig = 0x4C656201;

    void CopyTexts(const TLinkTextRec& record) {
        Texts = record.Texts;
    }

    size_t SizeOf() const {
        return offsetof(TLinkWithTextRec, Texts) + Texts.SizeOf();
    }

    static int ByUidFnv(const TLinkWithTextRec* a, const TLinkWithTextRec* b) {
        int uidComparison = CompareUids(a, b);
        return uidComparison != 0 ? uidComparison : compare(a->Fnv, b->Fnv);
    }

};

MAKESORTERTMPL(TLinkWithTextRec, ByUidFnv);

struct TLinkExtSaveDateRecWithNames {
  ui64       UrlId; // for source url
  ui64       UrlIdTo;
  ui64       Fnv;
  ui32       HostId; // for source url
  ui32       HostIdTo; // for mapping to hostname
  ui32       DocId;
  ui16       DateDiscovered;
  redir_t    Names; // "from url" and "to url"

  static const ui32 RecordSig = 0x21088399;

    TLinkExtSaveDateRecWithNames(
        ui64 urlIdFrom,
        ui64 urlIdTo,
        ui64 fnv,
        ui32 hostIdFrom,
        ui32 hostIdTo,
        const char* sourceUrl,
        const char* dstUrl)
      : UrlId(urlIdFrom)
      , UrlIdTo(urlIdTo)
      , Fnv(fnv)
      , HostId(hostIdFrom)
      , HostIdTo(hostIdTo)
      , DocId(0)
      , DateDiscovered(0)
        {

        strcpy(Names, sourceUrl);
        size_t sourceUrlLen = strlen(sourceUrl);
        strcpy(Names + sourceUrlLen + 1, dstUrl);
    }

    TLinkExtSaveDateRecWithNames() {
        TLinkExtSaveDateRecWithNames(0, 0, 0, 0, 0, "","");
    }

    size_t SizeOf() const {
      return offsetof(TLinkExtSaveDateRecWithNames, Names) + strlen(Names) + strlen(strchr(Names, '\0') + 1) + 2;
    }

    static int ByUidFnv(const TLinkExtSaveDateRecWithNames *a, const TLinkExtSaveDateRecWithNames *b) {
        int uidComparison = CompareUids(a, b);
        return uidComparison != 0 ? uidComparison : compare(a->Fnv, b->Fnv);
    }

    static int ByFnv(const TLinkExtSaveDateRecWithNames *a, const TLinkExtSaveDateRecWithNames *b) {
        return compare(a->Fnv, b->Fnv);
    }

    static int ByDstUid(const TLinkExtSaveDateRecWithNames *a, const TLinkExtSaveDateRecWithNames *b) {
        int ret = compare(a->HostIdTo, b->HostIdTo);
        return ret ? ret : compare(a->UrlIdTo, b->UrlIdTo);
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }
};

MAKESORTERTMPL(TLinkExtSaveDateRecWithNames, ByUidFnv);
MAKESORTERTMPL(TLinkExtSaveDateRecWithNames, ByFnv);
MAKESORTERTMPL(TLinkExtSaveDateRecWithNames, ByDstUid);

template<typename T>
void ClearMeFully(T* ptr) {
    memset(ptr, 0, sizeof(T));
}

struct TPreparatFillerInfoBase {
    ui32 DocId;
    ui32 Flags;
    ui32 SourceDate;
};

struct TPreparatFillerInfoMedium {
    ui32 DocId;
    ui32 DstFakeId;
    ui32 SrcUrl;
    ui32 SrcSegment;

    ui32 Language;
    typedef TLinkPreparatData TExtInfo;

    static const ui32 RecordSig = 0x22356361;

    TPreparatFillerInfoMedium() {
        ClearMeFully(this);
    }

    static int ByDst(const TPreparatFillerInfoMedium* lhs, const TPreparatFillerInfoMedium* rhs) {
        return ::compare(lhs->DstFakeId, rhs->DstFakeId);
    }
};

MAKESORTERTMPL(TPreparatFillerInfoMedium, ByDst);

struct TPreparatFillerInfo {
    ui32 DocId;
    ui32 HostId;
    ui64 UrlId;

    ui32 Language;
    typedef TLinkPreparatData TExtInfo;

    static const ui32 RecordSig = 0x22356362;


    TPreparatFillerInfo() {
        ClearMeFully(this);
    }

    static int ByDocIdUid(const TPreparatFillerInfo* lhs, const TPreparatFillerInfo* rhs) {
        int cmp = 0;
        cmp = cmp ? cmp : ::compare(lhs->DocId, rhs->DocId);
        cmp = cmp ? cmp : ::compare(lhs->HostId, rhs->HostId);
        cmp = cmp ? cmp : ::compare(lhs->UrlId, rhs->UrlId);
        return cmp;
    }
};

MAKESORTERTMPL(TPreparatFillerInfo, ByDocIdUid);

struct TLinkSource1 {
    ui32   DocId;
    ui32   LangRegion;
    ui32   UrlFlags;
    ui8    SourceId;
    url_t  Name;

    static const ui32 RecordSig = 0xB4621618;

    size_t SizeOf() const {
        return offsetof(TLinkSource1, Name) + strlen(Name) + 1;
    }

    static int ByDocId(const TLinkSource1 *a, const TLinkSource1 *b) {
        return ::compare(a->DocId, b->DocId);
    }
};

struct TExtractedPrepInfo {
    ui64 UrlIdTo;
    ui64 TrueDstFnv;
    ui32 SrcSeg;
    ui32 DstLang;
    ui32 RecNum;

    static const ui32 RecordSig = 0xB4621619;
};

#pragma pack(pop)
#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic pop
#endif
