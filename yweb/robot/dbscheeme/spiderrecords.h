#pragma once

#include <algorithm>

#include <library/cpp/digest/old_crc/crc.h> //for caclulating crc of CStream

#include "baserecords.h"
#include "mergerecords.h"
#include "urlflags.h"
#include <library/cpp/microbdb/sorterdef.h>

#ifdef __GNUC__
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#pragma pack(push, 4)

#if defined (_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable:4706) /*assignment within conditional expression*/
#   pragma warning(disable:4267) /*conversion from 'size_t' to 'type', possible loss of data*/
#endif

struct TLogHeader {
    ui32    Signature;
    ui32    Type;

    static const ui32 RecordSig = 0x12346201;
};

struct TUrlQuel {
    TDataworkDate ModTime;
    ui32          Flags;
    ui8           Hops;
    url_t         Name;

    static const ui32 RecordSig = 0x12346212;
    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    class TChecker {
    public:
        TChecker()
            : urlsnum (0)
            , urlstotal (0) {}

        void CheckRecord(const TUrlQuel *UrlQuel, const char *file_name = nullptr) {
            if (!UrlQuel)
                ythrow yexception() << "Given record is null";

            if (!(*UrlQuel->Name && (strlen(UrlQuel->Name) < URL_MAX)))
                ythrow yexception() << "Corrupted Name field in TUrlQuel record (file '" <<  file_name << "')";

            if (UrlQuel->Name[0] == '/') { // url
                if (urlsnum <= 0)
                    ythrow yexception() << "Too many urls record for the current host '" <<
                        hostname << "', file '" <<  file_name << "', urlstotal " <<  urlstotal;

                --urlsnum;
            } else { // host
                if (urlsnum != 0)
                    ythrow yexception() << "Too little url records (next host is '" <<
                                    UrlQuel->Name << "', file '" <<  file_name << "', urlsnum " <<  urlsnum << ", orighost is '" <<  hostname << "', urlstotal " <<  urlstotal << ")";

                if (UrlQuel->ModTime < 2)
                    ythrow yexception() << "Too little url records (host is '" << UrlQuel->Name << "', file '" << file_name << "', ModTime field " << UrlQuel->ModTime << ")";
                urlsnum = UrlQuel->ModTime - 1;
                urlstotal = UrlQuel->ModTime;
                strlcpy(hostname, UrlQuel->Name, 32);
            }
        }

    protected:
        ui32 urlsnum;
        ui32 urlstotal;
        char hostname[32];
    };
};

struct TRobotsLogelOld {
    TDataworkDate    LastAccess;
    host_robots_old_t    Packed;

    static const ui32 RecordSig = 0x12347213;
    size_t SizeOf() const {
        return offsetofthis(Packed.Data) + strlen(Packed.Data) + 1 + Packed.Size;
    }
};

struct TRobotsLogel {
    TDataworkDate    LastAccess;
    host_multirobots_t    Packed;

    static const ui32 RecordSig = 0x12374132;
    size_t SizeOf() const {
        return offsetofthis(Packed.Data) + strlen(Packed.Data) + 1 + Packed.Size;
    }
};

struct THostLogelHeader {
    i32     Start;
    i32     Finish;
    ui32    Ip;
    ui32    NumUrls;
    ui32    NumDocs;
    ui32    Status;
};

struct THostLogel : public THostLogelHeader {
    dbscheeme::host_t  Name;

    static const ui32 RecordSig = 0x12346214;
    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }
};

struct TNewUrlLogel : public TNewUrlHeader {
    TDataworkDate   CountryTimeStamp;
    ui8             CountryId;
    ui8             SourceId;
    url_t           Name;

    static const ui32 RecordSig = 0x13247216;
    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByName(const TNewUrlLogel *a, const TNewUrlLogel *b) {
        return stricmp(a->Name, b->Name);
    }

    static int ByNameHop(const TNewUrlLogel *a, const TNewUrlLogel *b) {
        int ret = stricmp(a->Name, b->Name);
        return ret ? ret : ::compare(a->Hops, b->Hops);
    }

    static int ByHostNameHopLastMod(const TNewUrlLogel *a, const TNewUrlLogel *b) {
        const char* sla = strchr(a->Name, '/');
        const char* slb = strchr(b->Name, '/');
        int lena = sla ? sla - a->Name : strlen(a->Name);
        int lenb = slb ? slb - b->Name : strlen(b->Name);
        int minlen = (int)std::min(lena, lenb);
        int ret = memcmp(a->Name, b->Name, minlen);
        if (ret)
            return ret;
        if (lena != lenb)
            return ::compare(lena, lenb);
        if (a->Hops != b->Hops)
            return ::compare(a->Hops, b->Hops);
        return ::compare(b->AddTime, a->AddTime);
    }

    static size_t OffsetOfName() {
        static TNewUrlLogel rec;
        return rec.OffsetOfNameImpl();
    }

private:
    size_t OffsetOfNameImpl() const {
        return offsetofthis(Name);
    }
};

MAKESORTERTMPL(TNewUrlLogel, ByName);
MAKESORTERTMPL(TNewUrlLogel, ByNameHop);
MAKESORTERTMPL(TNewUrlLogel, ByHostNameHopLastMod);

struct TNewUrlLogelOld : public TNewUrlHeader {
    ui8     SourceId;
    url_t   Name;

    static const ui32 RecordSig = 0x13247215;
    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }

    static int ByName(const TNewUrlLogelOld *a, const TNewUrlLogelOld *b) {
        return stricmp(a->Name, b->Name);
    }

    static int ByNameHop(const TNewUrlLogelOld *a, const TNewUrlLogelOld *b) {
        int ret = stricmp(a->Name, b->Name);
        return ret ? ret : ::compare(a->Hops, b->Hops);
    }

    static int ByHostNameHopLastMod(const TNewUrlLogelOld *a, const TNewUrlLogelOld *b) {
        const char* sla = strchr(a->Name, '/');
        const char* slb = strchr(b->Name, '/');
        int lena = sla ? sla - a->Name : strlen(a->Name);
        int lenb = slb ? slb - b->Name : strlen(b->Name);
        int minlen = (int)std::min(lena, lenb);
        int ret = memcmp(a->Name, b->Name, minlen);
        if (ret)
            return ret;
        if (lena != lenb)
            return ::compare(lena, lenb);
        if (a->Hops != b->Hops)
            return ::compare(a->Hops, b->Hops);
        return ::compare(b->AddTime, a->AddTime);
    }

    static size_t OffsetOfName() {
        static TNewUrlLogelOld rec;
        return rec.OffsetOfNameImpl();
    }

private:
    size_t OffsetOfNameImpl() const {
        return offsetofthis(Name);
    }
};

MAKESORTERTMPL(TNewUrlLogelOld, ByName);
MAKESORTERTMPL(TNewUrlLogelOld, ByNameHop);
MAKESORTERTMPL(TNewUrlLogelOld, ByHostNameHopLastMod);

struct TUpdUrlLogel : public TUpdUrlHeader {
    url_t       Name;

    static const ui32 RecordSig = 0x12347216;

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }
};

struct TUpdSitemapUrlLogel : public TUpdUrlHeader {
    url_t       Name;

    static const ui32 RecordSig = 0x12347217;

    size_t SizeOf() const {
        return offsetofthis(Name) + strlen(Name) + 1;
    }
};

struct TRedirUrlLogel {
    TDataworkDate LastAccess;
    ui32          Size;
    ui32          Flags;
    ui32          HttpCode;
    ui8           Hops;
    redir_t       Names;

    static const ui32 RecordSig = 0x12347207;

    static size_t OffsetOfNames() {
        static TRedirUrlLogel rec;
        return rec.OffsetOfNamesImpl();
    }

    size_t SizeOf() const {
        return offsetofthis(Names) + strlen(Names) + strlen(strchr(Names, '\0') + 1) + 2;
    }

private:
    size_t OffsetOfNamesImpl() const {
        return offsetofthis(Names);
    }
};

struct TSitemapRedirUrlLogel : public TRedirUrlLogel {

    static const ui32 RecordSig = 0x12347208;

    size_t SizeOf() const {
        return offsetofthis(Names) + strlen(Names) + strlen(strchr(Names, '\0') + 1) + 2;
    }
};


struct TDocLogel : public TIndUrlDoc {

    static const ui32 RecordSig = 0x1234722A;

    size_t SizeOf() const {
        return offsetofthis(DataStream) + DataStream.SizeOf();
    }

    static int ByCrc(const TDocLogel *a, const TDocLogel *b) {
        return ::compare(a->Crc, b->Crc);
    }
};

MAKESORTERTMPL(TDocLogel, ByCrc);

struct TSitemapLogel : public TIndUrlHeader {
    packed_stream_t DataStream;

    static const ui32 RecordSig = 0x1234722B;

    size_t SizeOf() const {
        return offsetofthis(DataStream) + DataStream.SizeOf();
    }

    static int ByCrc(const TSitemapLogel *a, const TSitemapLogel *b) {
        return ::compare(a->Crc, b->Crc);
    }
};

MAKESORTERTMPL(TSitemapLogel, ByCrc);

struct TWebmasterSitemapLogel {
    TDataworkDate Added;

    typedef char hostandurl_t[sizeof(dbscheeme::host_t) + sizeof(url_t)];
    hostandurl_t Names;

    static const ui32 RecordSig = 0x1234722C;

    static size_t OffsetOfNames() {
        static TWebmasterSitemapLogel rec;
        return rec.OffsetOfNamesImpl();
    }

    size_t SizeOf() const {
        return offsetofthis(Names)
                    + strlen(Names) + 1                     // first string
                    + strlen(strchr(Names, '\0') + 1) + 1;  // second string
    }

private:
    size_t OffsetOfNamesImpl() const {
        return offsetofthis(Names);
    }
};

struct TDocLogelOld : public TIndUrlHeaderOld {
    packed_stream_t DataStream;

    static const ui32 RecordSig = 0x1234720A;

    size_t SizeOf() const {
        return offsetofthis(DataStream) + DataStream.SizeOf();
    }

    static int ByCrc(const TDocLogelOld *a, const TDocLogelOld *b) {
        return ::compare(a->Crc, b->Crc);
    }
};

MAKESORTERTMPL(TDocLogelOld, ByCrc);

struct TForeignUrlLogel : public urlid_t, public TNewUrlHeader {
    ui32    SrcSeg;
    url_t   Name;

    static const ui32 RecordSig = 0x12347219;

    static size_t OffsetOfName() {
        static TForeignUrlLogel rec;
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

MAKESORTERTMPL(TForeignUrlLogel, ByUid);

struct TIpGeoRecOld {
    ui32    Ip;
    ui32    Region;

    static const ui32 RecordSig = 0x12347220;
};

struct TIpGeoRec {
    ui64    IpLow;
    ui64    IpHigh;
    ui32    Region;
    ui32    Type;

    void SetIpv4(ui32 v) {
        IpHigh = 0;
        IpLow = v;
    }
    ui32 GetIpv4() const {
        return (ui32)IpLow;
    }
    static const ui32 RecordSig = 0x12347221;
};

#if defined (_MSC_VER)
#   pragma warning(pop)
#endif

#pragma pack(pop)
#ifdef __GNUC__
#   pragma GCC diagnostic pop
#endif
