#pragma once

#include <stddef.h>
#include <string.h>
#include <time.h>

// util
#include <util/system/defaults.h>
#include <util/system/compat.h>

#include <kernel/urlnorm/urlnorm.h>

#include <yweb/config/hostcrc.h>

#include <yweb/protos/robot.pb.h>

#include "dbtypes.h"
#include <library/cpp/microbdb/sorterdef.h>
#include "urlflags.h"

class TUrlExtData;
class TRssLinkExtData;

#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#pragma pack(push, 4)

struct THostRec {
    ui64            Crc;
    TDataworkDate   AddTime;
    TDataworkDate   LastAccess;
    ui32            HostId;
    ui32            Ip;
    ui32            Urls;
    ui32            Docs;
    ui32            Queued;
    ui32            AntiqUrls;
    ui32            Antiquity;
    ui16            Status;
    ui16            Penalty;
    dbscheeme::host_t          Name;

    static const ui32 RecordSig = 0x12346101;
    size_t SizeOf() const {
        return offsetof(THostRec,Name) + strlen(Name) + 1;
    }

    operator crc64_t() const {
        return Crc;
    }
    operator hostid_t() const {
        return HostId;
    }

    ui32 SegCrc() const {
        return (ui32)HostCrc4Config(Name);
    }

    static int ByHostId(const THostRec *a, const THostRec *b) {
        return compare(a->HostId, b->HostId);
    }
    static int ByCrc(const THostRec *a, const THostRec *b) {
        return compare(a->Crc, b->Crc);
    }
    static int ByName(const THostRec *a, const THostRec *b) {
        return stricmp(a->Name, b->Name);
    }
};

MAKESORTERTMPL(THostRec, ByHostId);
MAKESORTERTMPL(THostRec, ByCrc);
MAKESORTERTMPL(THostRec, ByName);

struct TUrlBase {
    ui64            UrlId;
    TDataworkDate   AddTime;
    TDataworkDate   HttpModTime;
    TDataworkDate   LastAccess;
    ui32            HostId;
    ui32            Size;
    ui32            Flags; // defined in namespace UrlFlags
    ui32            HttpCode;
    ui8             MimeType; // defined in enum MimeTypes
    i8              Encoding; // defined in enum ECharset
    ui8             Language; // defined in enum ELanguage
    ui8             Status; // defined in namespace UrlStatus

    static const ui32 RecordSig = 0x12346102;

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }
    operator crc64_t() const {
        return crc64_t(UrlId);
    }

    static int ByUid(const TUrlBase *a, const TUrlBase *b) {
        return CompareUids(a, b);
    }
    static int ByUrlId(const TUrlBase *a, const TUrlBase *b) {
        return ::compare(a->UrlId, b->UrlId);
    }
    static int ByUidTime(const TUrlBase *a, const TUrlBase *b) {
        int ret = CompareUids(a, b);
        return ret ? ret : b->LastAccess - a->LastAccess;  // reverse by time
    }
    static int ForSemidups(const TUrlBase *a, const TUrlBase *b) {
        int ret = ::compare(a->Size, b->Size);             // Size = GroupId
        ret = ret ? ret : ::compare(a->HostId, b->HostId);
        return ret ? ret : a->LastAccess - b->LastAccess;  // LastAccess = starlen(name)
    }
};

MAKESORTERTMPL(TUrlBase, ByUid);
MAKESORTERTMPL(TUrlBase, ByUrlId);
MAKESORTERTMPL(TUrlBase, ByUidTime);
MAKESORTERTMPL(TUrlBase, ForSemidups);

struct TUrlRecNoProto: public TUrlBase {
    TDataworkDate   ModTime;
    ui32            DocId;
    ui8             Hops;
    url_t           Name;

    inline void AssignName(char const *name) {
        if (Y_UNLIKELY(strlcpy(&Name[0], name, sizeof(url_t)) >= sizeof(url_t)))
            ythrow yexception() << "trying to copy too long name\n";
    }

    inline void AssignName(char const *name, size_t size) {
        if (size >= sizeof(url_t))
            ythrow yexception() << "trying to copy too long name\n";
        memcpy(&Name[0], name, size);
        Name[size] = '\0';
    }

    inline void Assign(TUrlRecNoProto const& rhs) {
        copy_header<TUrlBase>(this, &rhs);
        ModTime = rhs.ModTime;
        DocId = rhs.DocId;
        Hops = rhs.Hops;
        AssignName(rhs.Name);
    }

    static const ui32 RecordSig = 0x12346103;
    size_t SizeOf() const {
        return strchr(Name, 0) + 1 - (char*)(void*)this;
    }

    static int ByHostName(const TUrlRecNoProto *a, const TUrlRecNoProto *b) {
        int ret = ::compare(a->HostId, b->HostId);
        return ret? ret : stricmp(a->Name, b->Name);
    }
    static int ByDocHost(const TUrlRecNoProto *a, const TUrlRecNoProto *b) {
        int ret = ::compare(a->DocId, b->DocId);
        ret = ret ? ret : ::compare(a->HostId, b->HostId);
        ret = ret ? ret : ::compare(a->Flags & UrlFlags::WAS_SEMIDUP, b->Flags & UrlFlags::WAS_SEMIDUP);
        ret = ret ? ret : (int)strlen(a->Name) - (int)strlen(b->Name);
        return ret ? ret : ::compare(a->UrlId, b->UrlId);
    }
    static int ByDoc(const TUrlRecNoProto *a, const TUrlRecNoProto *b) {
        return ::compare(a->DocId, b->DocId);
    }
    static int ByHostUrl(const TUrlRecNoProto *a, const TUrlRecNoProto *b) {
        int ret = ::compare(a->HostId, b->HostId);
        return ret ? ret : ::compare(a->UrlId, b->UrlId);
    }
};

MAKESORTERTMPL(TUrlRecNoProto, ByUid);
MAKESORTERTMPL(TUrlRecNoProto, ByUrlId);
MAKESORTERTMPL(TUrlRecNoProto, ByUidTime);
MAKESORTERTMPL(TUrlRecNoProto, ForSemidups);
MAKESORTERTMPL(TUrlRecNoProto, ByHostName);
MAKESORTERTMPL(TUrlRecNoProto, ByDocHost);
MAKESORTERTMPL(TUrlRecNoProto, ByDoc);
MAKESORTERTMPL(TUrlRecNoProto, ByHostUrl);

struct TUrlRec: public TUrlRecNoProto {
    typedef  TUrlExtData TExtInfo;
    static const ui32 RecordSig = 0x12348103;
    static int ForCatfilter(const TUrlRec *a, const TUrlRec *b) {
        int ret = ::compare(a->DocId, b->DocId);
        ret = ret ? ret : b->LastAccess - a->LastAccess;  // LastAccess = citind << 8 + strlen
        return ret ? ret : CompareUids(a, b);
    }

    static int ByAccessTime(const TUrlRec *a, const TUrlRec *b) {
        int ret = a->LastAccess - b->LastAccess;
        return ret ? ret : CompareUids(a, b);
    }

    static int ByHops(const TUrlRec* a, const TUrlRec* b) {
        return ::compare(a->Hops, b->Hops);
    }
};

MAKESORTERTMPL(TUrlRec, ByUid);
MAKESORTERTMPL(TUrlRec, ByUrlId);
MAKESORTERTMPL(TUrlRec, ByUidTime);
MAKESORTERTMPL(TUrlRec, ForSemidups);
MAKESORTERTMPL(TUrlRec, ByHostName);
MAKESORTERTMPL(TUrlRec, ForCatfilter);
MAKESORTERTMPL(TUrlRec, ByDocHost);
MAKESORTERTMPL(TUrlRec, ByDoc);
MAKESORTERTMPL(TUrlRec, ByHostUrl);
MAKESORTERTMPL(TUrlRec, ByAccessTime);
MAKESORTERTMPL(TUrlRec, ByHops);

#ifdef COMPAT_ROBOT_STABLE
#   define TUrlRec TUrlRecNoProto
#endif

struct TDocUidRec {
    ui64            UrlId;
    ui32            HostId;
    ui32            DocId;

    static const ui32 RecordSig = 0x12346105;
    operator docid_t() const {
        return DocId;
    }

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TDocUidRec *a, const TDocUidRec *b) {
        return CompareUids(a, b);
    }

    static int ByUidDoc(const TDocUidRec *a, const TDocUidRec *b) {
        int r = CompareUids(a, b);
        if (r)
            return r;
        return ::compare(a->DocId, b->DocId);
    }

    static int ByDocUid(const TDocUidRec *a, const TDocUidRec *b) {
        int ret = ::compare(a->DocId, b->DocId);
        return ret? ret : CompareUids(a, b);
    }


    static int ByHostDoc(const TDocUidRec *a, const TDocUidRec *b) {
        int ret = ::compare(a->HostId, b->HostId);
        ret = ret ? ret : ::compare(a->DocId, b->DocId);
        return ret ? ret : ::compare(a->UrlId, b->UrlId);
    }
};

struct TDocUidTimeRec : public TDocUidRec
{
    TDataworkDate AddTime;

    static const ui32 RecordSig = 0x132946B;

    TDocUidTimeRec(const TDocUidRec& rec)
        : TDocUidRec(rec)
        , AddTime(0)
    {
    }

    ui16 GetDateInDays() const
    {
        return ui16(AddTime / (60 * 60 * 24));
    }
};

MAKESORTERTMPL(TDocUidTimeRec, ByUid);
MAKESORTERTMPL(TDocUidTimeRec, ByDocUid);
MAKESORTERTMPL(TDocUidTimeRec, ByHostDoc);

struct TDocUidRecCSUid : public TDocUidRec
{
    ui64        CSUid;

    static int ByUid(const TDocUidRecCSUid *a, const TDocUidRecCSUid *b) {
        int ret = CompareUids(a, b);
        ret = ret ? ret : ::compare(a->CSUid, b->CSUid);
        return ret ? ret : ::compare(b->DocId, a->DocId);
    }
    static int ByUidDoc(const TDocUidRecCSUid *a, const TDocUidRecCSUid *b) {
        int ret = CompareUids(a, b);
        ret = ret ? ret : ::compare(b->DocId, a->DocId);
        return ret ? ret : ::compare(a->CSUid, b->CSUid);
    }
    static const ui32 RecordSig = 0x12346117;
};

MAKESORTERTMPL(TDocUidRecCSUid, ByUid);
MAKESORTERTMPL(TDocUidRecCSUid, ByUidDoc);
MAKESORTERTMPL(TDocUidRecCSUid, ByDocUid);
MAKESORTERTMPL(TDocUidRecCSUid, ByHostDoc);

struct TUidPr {
    ui64        UrlId;
    ui32        HostId;
    ui32        PR;
    ui32        PRext;

    static const ui32 RecordSig = 0x12346113;

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }
    static int ByUid(const TUidPr *a, const TUidPr *b) {
        return CompareUids(a, b);
    }

};

MAKESORTERTMPL(TUidPr, ByUid);
MAKESORTERTMPL(TDocUidRec, ByUid);
MAKESORTERTMPL(TDocUidRec, ByUidDoc);
MAKESORTERTMPL(TDocUidRec, ByDocUid);
MAKESORTERTMPL(TDocUidRec, ByHostDoc);

struct TSigDocRec {
    ui64        Crc;
    ui32        HostId;
    ui32        DocId;

    static const ui32 RecordSig = 0x12346106;

    operator hostid_t() const {
        return HostId;
    }

    static int ByHostSig(const TSigDocRec *a, const TSigDocRec *b) {
        int ret = ::compare(a->HostId, b->HostId);
        return ret ? ret : ::compare(a->Crc, b->Crc);
    }
    static int ByDoc(const TSigDocRec *a, const TSigDocRec *b) {
        return ::compare(a->DocId, b->DocId);
    }

    static int BySigHost(const TSigDocRec *a, const TSigDocRec *b) {
        int ret =::compare(a->Crc, b->Crc);
        return ret ? ret : ::compare(a->HostId, b->HostId);
    }
};

MAKESORTERTMPL(TSigDocRec, ByHostSig);
MAKESORTERTMPL(TSigDocRec, ByDoc);
MAKESORTERTMPL(TSigDocRec, BySigHost);

struct TSigDocPrnRec : public TSigDocRec {
    float Pruning;

    static const ui32 RecordSig = 0x12346111;
};

struct TDocGroupRec {
    ui32    DocId;
    ui32    GroupId;

    static const ui32 RecordSig = 0x12346107;

    operator docid_t() const {
        return DocId;
    }

    TDocGroupRec() {}

    TDocGroupRec(const ui32 docId, const ui32 groupId)
        : DocId(docId)
        , GroupId(groupId)
        {}

    static int ByDocGroup(const TDocGroupRec *a, const TDocGroupRec *b) {
        int ret = ::compare(a->DocId, b->DocId);
        return ret? ret : ::compare(a->GroupId, b->GroupId);
    }
    static int ByGroupDoc(const TDocGroupRec *a, const TDocGroupRec *b) {
        int ret = :: compare(a->GroupId, b->GroupId);
        return ret? ret : ::compare(a->DocId, b->DocId);
    }
};

MAKESORTERTMPL(TDocGroupRec, ByDocGroup);
MAKESORTERTMPL(TDocGroupRec, ByGroupDoc);

struct TCitindRec {
    ui32    Citind;
    ui8     Flags;
    dbscheeme::host_t  Name;

    static const ui32 RecordSig = 0x12346108;
    size_t SizeOf() const {
        return offsetof(TCitindRec,Name) + strlen(Name) + 1;
    }
};

struct TFuzzyDocSignature {
    ui64    Fnv;
    ui32    GroupId;
    ui32    DocId;
    ui32    DocLen;

    static const ui32 RecordSig = 0x12346109;

    static int ByFnvGroupLen(const TFuzzyDocSignature *a, const TFuzzyDocSignature *b) {
        int ret = ::compare(a->Fnv, b->Fnv);
        ret = ret? ret : a->GroupId - b->GroupId;
        return ret? ret : a->DocLen - b->DocLen;
    }
     bool SameFnv(const TFuzzyDocSignature* a) const
    {
        return Fnv == a->Fnv;
    }
    void CopyFnv(const TFuzzyDocSignature* a)
    {
        Fnv = a->Fnv;
    }
};

MAKESORTERTMPL(TFuzzyDocSignature, ByFnvGroupLen);

struct THostLogRec {
    TDataworkDate   Start;
    TDataworkDate   Finish;
    ui32            HostId;
    ui32            Ip;
    ui32            NumUrls;
    ui32            NumDocs;
    ui32            Status;

    static const ui32 RecordSig = 0x12346110;
    operator hostid_t() const {
        return HostId;
    }

    static int ByHostTime(const THostLogRec *a, const THostLogRec *b) {
        int ret = ::compare(a->HostId, b->HostId);
        return ret ? ret : a->Finish - b->Finish;
    }
};

MAKESORTERTMPL(THostLogRec, ByHostTime);

struct TRobotsRecOld {
    TDataworkDate   LastAccess;
    ui32            HostId;
    robots_old_t    Packed;

    static const ui32 RecordSig = 0x12347111;
    size_t SizeOf() const {
        return offsetof(TRobotsRecOld, Packed.Data) + Packed.Size;
    }
    operator hostid_t() const {
        return HostId;
    }

    static int ByHostTime(const TRobotsRecOld *a, const TRobotsRecOld *b) {
        int ret = ::compare(a->HostId, b->HostId);
        return ret ? ret : a->LastAccess - b->LastAccess;
    }
};

MAKESORTERTMPL(TRobotsRecOld, ByHostTime);

struct TRobotsRec {
    TDataworkDate   LastAccess;
    ui32            HostId;
    multirobots_t   Packed;

    static const ui32 RecordSig = 0x12347444;

    static size_t OffsetOfPacked() {
        static TRobotsRec rec;
        return rec.OffsetOfPackedImpl();
    }

    size_t SizeOf() const {
        return offsetof(TRobotsRec, Packed.Data) + Packed.Size;
    }
    operator hostid_t() const {
        return HostId;
    }

    static int ByHostTime(const TRobotsRec *a, const TRobotsRec *b) {
        int ret = ::compare(a->HostId, b->HostId);
        return ret ? ret : a->LastAccess - b->LastAccess;
    }

    static int ByHostInvTime(const TRobotsRec *a, const TRobotsRec *b) {
        int ret = ::compare(a->HostId, b->HostId);
        return ret ? ret : b->LastAccess - a->LastAccess;
    }

private:
    size_t OffsetOfPackedImpl() const {
        return offsetofthis(Packed);
    }
};

MAKESORTERTMPL(TRobotsRec, ByHostTime);
MAKESORTERTMPL(TRobotsRec, ByHostInvTime);

typedef char blobtext_t[20<<10];

struct TDocBlobRec {
    ui32       DocId;
    blobtext_t Data;

    static const ui32 RecordSig = 0x12347113;
    size_t SizeOf() const {
        return offsetof(TDocBlobRec, Data) + strlen(Data) + 1;
    }

    static int ByDoc(const TDocBlobRec *a, const TDocBlobRec *b) {
        return ::compare(a->DocId, b->DocId);
    }
};

MAKESORTERTMPL(TDocBlobRec, ByDoc);

struct TDocTimeBlobRec
{
    ui32 DocId;
    TDataworkDate Time;
    blobtext_t Data;

    static const ui32 RecordSig = 0x12346300;

    size_t SizeOf() const {
        return offsetof(TDocTimeBlobRec, Data) + strlen(Data) + 1;
    }

    static int ByDoc(const TDocTimeBlobRec* a, const TDocTimeBlobRec* b) {
        return ::compare(a->DocId, b->DocId);
    }

    static int ByDocTime(const TDocTimeBlobRec* a, const TDocTimeBlobRec* b) {
        int ret = ::compare(a->DocId, b->DocId);
        return ret ? ret : ::compare(a->Time, b->Time);
    }
    static int ByDocDescTime(const TDocTimeBlobRec* a, const TDocTimeBlobRec* b) {
        int ret = ::compare(a->DocId, b->DocId);
        return ret ? ret: -::compare(a->Time, b->Time);
    }
};

MAKESORTERTMPL(TDocTimeBlobRec, ByDoc);
MAKESORTERTMPL(TDocTimeBlobRec, ByDocTime);
MAKESORTERTMPL(TDocTimeBlobRec, ByDocDescTime);

struct TFuzzyDocSignatureHost {
    ui64    Fnv;
    ui32    GroupId;
    ui32    DocId;
    ui32    DocLen;
    ui32    HostId;

    static const ui32 RecordSig = 0x12346112;

    static int ByLen(const TFuzzyDocSignatureHost *a, const TFuzzyDocSignatureHost *b) {
        return ::compare(a->DocLen, b->DocLen);
    }

    static int ByGroupLen(const TFuzzyDocSignatureHost *a, const TFuzzyDocSignatureHost *b) {
        int ret = ::compare(a->GroupId, b->GroupId);
        return ret ? ret : TFuzzyDocSignatureHost::ByLen(a, b);
    }

    static int ByFnvGroupLen(const TFuzzyDocSignatureHost *a, const TFuzzyDocSignatureHost *b) {
        int ret = ::compare(a->Fnv, b->Fnv);
        ret = ret ? ret : ::compare(a->HostId, b->HostId);
        return ret ? ret : TFuzzyDocSignatureHost::ByGroupLen(a, b);
    }

    static int ByFnvGroupLenDocId(const TFuzzyDocSignatureHost *a, const TFuzzyDocSignatureHost *b) {
        int ret = TFuzzyDocSignatureHost::ByFnvGroupLen(a, b);
        return ret ? ret : ::compare(a->DocId, b->DocId);
    }

    static int ByGroupLenDocId(const TFuzzyDocSignatureHost *a, const TFuzzyDocSignatureHost *b) {
        int ret = TFuzzyDocSignatureHost::ByGroupLen(a, b);
        return ret ? ret : ::compare(a->DocId, b->DocId);
    }

    TFuzzyDocSignatureHost()
        : Fnv(0)
        , GroupId(0)
        , DocId(0)
        , DocLen(0)
        , HostId(0) { }

    bool Empty() const {
        return Fnv == 0 && GroupId == 0 && DocId == 0 && DocLen == 0 && HostId == 0;
    }

    void Clear() {
        Fnv = 0;
        GroupId = 0;
        DocId = 0;
        DocLen = 0;
        HostId = 0;
    }

    bool SameFnv(const TFuzzyDocSignatureHost* a) const
    {
       return (Fnv == a->Fnv) && (HostId == a->HostId);
    }

    void CopyFnv(const TFuzzyDocSignatureHost* a)
    {
       Fnv = a->Fnv;
       HostId = a->HostId;
    }
};

MAKESORTERTMPL(TFuzzyDocSignatureHost, ByLen);
MAKESORTERTMPL(TFuzzyDocSignatureHost, ByGroupLen);
MAKESORTERTMPL(TFuzzyDocSignatureHost, ByFnvGroupLen);
MAKESORTERTMPL(TFuzzyDocSignatureHost, ByFnvGroupLenDocId);
MAKESORTERTMPL(TFuzzyDocSignatureHost, ByGroupLenDocId);

struct THostQueued {
    ui32 HostId;
    ui32 Count;
    ui8 Status;

    static const ui32 RecordSig = 0x12346114;

    operator hostid_t() const
    {
        return HostId;
    }
};

struct TQueuePartsRec {
    ui32 Hours;
    ui32 Parts;
    TDataworkDate TimeStamp;

    static const ui32 RecordSig = 0x14B35FAF;
};

struct THostQueuedHist {
    ui32 HostId;
    ui32 Count;
    TDataworkDate TimeStamp;
    ui8 Status;

    static const ui32 RecordSig = 0x12346115;

    operator hostid_t() const
    {
        return HostId;
    }
};

struct TIsNewQueuedHist {
    ui32 HostId;
    ui32 IsNewQueued;
    ui32 NotIsNewQueued;
    TDataworkDate TimeStamp;

    static const ui32 RecordSig = 0x45398752;

    operator hostid_t() const
    {
        return HostId;
    }
};

struct THostPolicyHist {
    ui32 HostId;
    ui32 Count;
    TDataworkDate TimeStamp;
    ui8 Policy;
    ui8 RobotZoneCode;

    static const ui32 RecordSig = 0xCEBABAC8;

    operator hostid_t() const
    {
        return HostId;
    }
};

struct THostPolicyHistOld {
    ui32 HostId;
    ui32 Count;
    TDataworkDate TimeStamp;
    ui8 Policy;

    static const ui32 RecordSig = 0xCEBABAC9;

    operator hostid_t() const
    {
        return HostId;
    }
};

struct TUrlHash {
    ui64 Hash;
    url_t Name;

    static const ui32 RecordSig = 0x12346116;

    operator crc64_t() const {
        return crc64_t(Hash);
    }
    static int ByHash(const TUrlHash *a, const TUrlHash *b) {
        return :: compare(a->Hash, b->Hash);
    }
    size_t SizeOf() const {
        return offsetof(TUrlHash,Name) + strlen(Name) + 1;
    }
};

MAKESORTERTMPL(TUrlHash, ByHash);

struct TCleanParamMainUrlRec {
    ui64        UrlId;
    ui64        GroupId;
    ui32        HostId;
    ui32        UrlLength;
    ui32        DocId;

    static const ui32 RecordSig = 0x12346155;

    operator urlid_t() const {
        return urlid_t(HostId, UrlId);
    }

    static int ByUid(const TCleanParamMainUrlRec *a, const TCleanParamMainUrlRec *b) {
        return CompareUids(a, b);
    }

};

MAKESORTERTMPL(TCleanParamMainUrlRec, ByUid);

struct TUrlGroupRec {
    ui64        UrlId;
    ui64        GroupId;

    static const ui32 RecordSig = 0x12346156;

    TUrlGroupRec(const ui64 urlId, const ui64 groupId)
        : UrlId(urlId)
        , GroupId(groupId) {}

    static int ByUrlId(const TUrlGroupRec *a, const TUrlGroupRec *b) {
        return compare(a->UrlId, b->UrlId);
    }

};

MAKESORTERTMPL(TUrlGroupRec, ByUrlId);

struct TGroupMainDocRec {
    ui64        GroupId;
    ui32        DocId;
    ui32        UrlLength;

    static const ui32 RecordSig = 0x12346157;

    TGroupMainDocRec(const ui64 groupId, const ui32 docId, const ui32 urlLength)
        : GroupId(groupId)
        , DocId(docId)
        , UrlLength(urlLength) {}

    static int ByDocId(const TGroupMainDocRec *a, const TGroupMainDocRec *b) {
        return compare(a->DocId, b->DocId);
    }

};

MAKESORTERTMPL(TGroupMainDocRec, ByDocId);

struct TDocumentSimHashOld {
    typedef ui32 TTitleHashValueType;
    typedef ui64 TSimHashValueType;

    ui32 DocId;
    ui32 HostId;
    ui32 DocLength;
    ui32 TitleHash;
    ui64 SimHash;

    static const ui32 RecordSig = 0x15048696;

    TDocumentSimHashOld()
        : DocId(docid_t::invalid)
        , HostId(0)
        , DocLength(0)
        , TitleHash(0)
        , SimHash(0) { }

    static int ByDoc(const TDocumentSimHashOld *a, const TDocumentSimHashOld *b) {
        if (a->DocId == b->DocId) {
            return 0;
        }
        if (a->DocId == docid_t::invalid) {
            return -1;
        }
        if (b->DocId == docid_t::invalid) {
            return 1;
        }
        return ::compare(a->DocId, b->DocId);
    }

    static int ByHostId(const TDocumentSimHashOld *a, const TDocumentSimHashOld *b) {
        return ::compare(a->HostId, b->HostId);
    }

    static int ByHostIdDocLength(const TDocumentSimHashOld *a, const TDocumentSimHashOld *b) {
        int ret = ::compare(a->HostId, b->HostId);
        return ret = ret ? ret : ::compare(a->DocLength, b->DocLength);
    }

    static int ByHostIdTitleHash(const TDocumentSimHashOld *a, const TDocumentSimHashOld *b) {
        int ret = ::compare(a->HostId, b->HostId);
        return ret ? ret : ::compare(a->TitleHash, b->TitleHash);
    }

    static int ByHostIdTitleHashDocLength(const TDocumentSimHashOld *a, const TDocumentSimHashOld *b) {
        int ret = ::compare(a->HostId, b->HostId);
        ret = ret ? ret : ::compare(a->TitleHash, b->TitleHash);
        return ret = ret ? ret : ::compare(a->DocLength, b->DocLength);
    }
};

struct TSpylogHubsRec: public urlid_t {
    double Rank;

    static const ui32 RecordSig = 0x23028736;
};

MAKESORTERTMPL(TSpylogHubsRec, ByUid);

struct TRssLinkRec {
    url_t Link;

    static const ui32 RecordSig = 0x14743821;
    typedef TRssLinkExtData TExtInfo;
};

struct TRssDateRec {
    ui32 HostId;
    ui64 UrlId;
    time_t Timestamp;

    static const ui32 RecordSig = 0x14743822;

    TRssDateRec() {}
    TRssDateRec(urlid_t uid, time_t timestamp)
        : HostId(uid.HostId)
        , UrlId(uid.UrlId)
        , Timestamp(timestamp)
    {
    }

    static int ByUid(const TRssDateRec* lhs, const TRssDateRec* rhs) {
        urlid_t first (lhs->HostId, lhs->UrlId);
        urlid_t second(rhs->HostId, rhs->UrlId);

        int ret = CompareUids(&first, &second);
        if (ret) {
            return ret;
        }

        return compare(lhs->Timestamp, rhs->Timestamp);
    }
};

MAKESORTERTMPL(TRssDateRec, ByUid);

struct TSuperDupRec: public urlid_t {
    ui32            GroupId;
    ui32            DocId;
    TDataworkDate   Timestamp;

    static const ui32 RecordSig = 0x23028738;

    TSuperDupRec()
        : urlid_t(0,0)
        , GroupId((ui32)-1)
        , DocId(docid_t::invalid)
        , Timestamp(0)
    {
    }

    static int ByHostTimeGroup(const TSuperDupRec *a, const TSuperDupRec *b) {
        int ret = ::compare(a->HostId, b->HostId);
        ret = ret ? ret : ::compare(a->Timestamp, b->Timestamp);
        ret = ret ? ret : ::compare(a->GroupId, b->GroupId);
        return ret ? ret : ::compare(a->DocId, b->DocId);
    }
};

MAKESORTERTMPL(TSuperDupRec, ByUid);
MAKESORTERTMPL(TSuperDupRec, ByHostTimeGroup);

struct TUidTime {
    TDataworkDate TimeStamp;
    ui32 HostId;
    ui64 UrlId;

    static const ui32 RecordSig = 0x45303593;
};

struct THostStatusInfoRec : public hostid_t {
    ui32 NewUrls;
    ui32 IndexedUrls;
    ui32 DeletedUrls;
    ui32 SemidupUrls;
    ui32 SRDeletedUrls;

    THostStatusInfoRec()
        : hostid_t((ui32)-1)
        {
        }

    THostStatusInfoRec(ui32 hostId, ui32 newUrls, int indexedUrls, int deletedUrls, int semidupUrls, int srDeletedUrls)
        : hostid_t(hostId)
        , NewUrls(newUrls)
        , IndexedUrls(indexedUrls)
        , DeletedUrls(deletedUrls)
        , SemidupUrls(semidupUrls)
        , SRDeletedUrls(srDeletedUrls)
        {
        }

    static const ui32 RecordSig = 0x25146738;
};

struct TSemidupHypothesis : public TDocumentSimHashOld {
    ui32 HypothesisId;
    ui32 GroupId;

    static const ui32 RecordSig = 0x15048695;

    TSemidupHypothesis()
        : TDocumentSimHashOld()
        , HypothesisId(0)
        , GroupId(0) { }

    explicit TSemidupHypothesis(const TDocumentSimHashOld &simhash, ui32 hypothesisId = 0, ui32 groupId = 0)
        : TDocumentSimHashOld(simhash)
        , HypothesisId(hypothesisId)
        , GroupId(groupId) { }

    static int ByGroupIdDocLen(const TSemidupHypothesis *a, const TSemidupHypothesis *b) {
        int ret = ::compare(a->GroupId, b->GroupId);
        return ret = ret ? ret : ::compare(a->DocLength, b->DocLength);
    }
};

MAKESORTERTMPL(TSemidupHypothesis, ByDoc);
MAKESORTERTMPL(TSemidupHypothesis, ByHostId);
MAKESORTERTMPL(TSemidupHypothesis, ByHostIdDocLength);
MAKESORTERTMPL(TSemidupHypothesis, ByHostIdTitleHash);
MAKESORTERTMPL(TSemidupHypothesis, ByHostIdTitleHashDocLength);
MAKESORTERTMPL(TSemidupHypothesis, ByGroupIdDocLen);

struct TDocIdChangeRec {
    ui64 UrlId;
    ui32 HostId;
    ui32 OldDocId;
    ui32 NewDocId;

    static const ui32 RecordSig = 0x15048694;

    TDocIdChangeRec()
        : UrlId(0)
        , HostId(0)
        , OldDocId(0)
        , NewDocId(0)
    { }

    static int ByOldDocIdUid(const TDocIdChangeRec *a, const TDocIdChangeRec *b) {
        int ret = ::compare(a->OldDocId, b->OldDocId);
        return ret? ret : CompareUids(a, b);
    }

    static int ByNewDocIdUid(const TDocIdChangeRec *a, const TDocIdChangeRec *b) {
        int ret = ::compare(a->NewDocId, b->NewDocId);
        return ret? ret : CompareUids(a, b);
    }
};

MAKESORTERTMPL(TDocIdChangeRec, ByOldDocIdUid);
MAKESORTERTMPL(TDocIdChangeRec, ByNewDocIdUid);

struct TSemidupDataRec : public TDocumentSimHashOld {
    ui64 Crc;
    ui32 SimhashVersion;

    static const ui32 RecordSig = 0x15048693;

    TSemidupDataRec()
        : TDocumentSimHashOld()
        , Crc(0)
        , SimhashVersion(0)
    { }

    static int ByHostSig(const TSemidupDataRec *a, const TSemidupDataRec *b) {
        int ret = ::compare(a->HostId, b->HostId);
        return ret ? ret : ::compare(a->Crc, b->Crc);
    }

    static int ByDoc(const TSemidupDataRec *a, const TSemidupDataRec *b) {
        return ::compare(a->DocId, b->DocId);
    }

    static int BySigHost(const TSemidupDataRec *a, const TSemidupDataRec *b) {
        int ret =::compare(a->Crc, b->Crc);
        return ret ? ret : ::compare(a->HostId, b->HostId);
    }
};

MAKESORTERTMPL(TSemidupDataRec, ByDoc);
MAKESORTERTMPL(TSemidupDataRec, ByHostId);
MAKESORTERTMPL(TSemidupDataRec, ByHostIdDocLength);
MAKESORTERTMPL(TSemidupDataRec, ByHostIdTitleHash);
MAKESORTERTMPL(TSemidupDataRec, ByHostIdTitleHashDocLength);
MAKESORTERTMPL(TSemidupDataRec, ByHostSig);
MAKESORTERTMPL(TSemidupDataRec, BySigHost);

struct TGluingToSemidupUrlRec : public urlid_t {
    static const ui32 RecordSig = 0x15048692;

    enum EReason {
        MordaToSemidup = 1,
        MainToOwnSemidup = 2,
        Other = 128
    };

    ui64 Crc;
    ui32 OldDocId;
    ui16 Reason;

    static int ByUid(const TGluingToSemidupUrlRec *a, const TGluingToSemidupUrlRec *b) {
        return urlid_t::ByUid(a, b);
    }
};

MAKESORTERTMPL(TGluingToSemidupUrlRec, ByUid);

struct TDocumentSimHash : public TDocumentSimHashOld {
    static const ui32 RecordSig = 0x15048691;

    ui64 SegSimhash;

    TDocumentSimHash()
        : TDocumentSimHashOld()
        , SegSimhash(0)
    { }
};

MAKESORTERTMPL(TDocumentSimHash, ByDoc);
MAKESORTERTMPL(TDocumentSimHash, ByHostId);
MAKESORTERTMPL(TDocumentSimHash, ByHostIdDocLength);
MAKESORTERTMPL(TDocumentSimHash, ByHostIdTitleHash);
MAKESORTERTMPL(TDocumentSimHash, ByHostIdTitleHashDocLength);

struct TDelHistory {
    static const ui32 RecordSig = 0x12348104;

    ui64 UrlId;
    ui64 HostFnv;
    ui32 HostId;
    ui32 Date;
    ui32 UrlFlags;
    ui32 FilterCode;
    ui32 HostStatus;
};

struct TDelHistoryRec : public TDelHistory {
    static const ui32 RecordSig = 0x12348105;

    ui8             Language; // defined in enum ELanguage
    ui8             Status; // defined in namespace UrlStatus
    url_t           Name;

    size_t SizeOf() const {
        return offsetof(TDelHistoryRec,Name) + strlen(Name) + 1;
    }
};


struct TDropPenaltyRec {
    static const ui32 RecordSig = 0x15148692;

    ui32 HostId;
    ui32 SourceId;
    ui32 Timestamp;
};


#pragma pack(pop)
#if defined(__GNUC__) || defined(__clang__)
#   pragma GCC diagnostic pop
#endif
