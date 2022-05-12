#pragma once

#include <util/system/defaults.h>
#include <util/system/maxlen.h>
#include <util/generic/string.h>
#include <util/generic/yexception.h>
#include <util/stream/output.h>

#include <library/cpp/microbdb/align.h>
#include <library/cpp/microbdb/sorterdef.h>

#include <library/cpp/robots_txt/constants.h>

#include "autozero.h"

#pragma pack(push, 4)

#define offsetofthis(A) ((char*)&A-(char*)this)

typedef ui32 allign_t;
typedef char url_t[URL_MAX];
namespace dbscheeme {
typedef char host_t[HOST_MAX];
}
typedef char signature_t[16];
typedef char linktext_t[LINKTEXT_MAX];
typedef char redir_t[2*URL_MAX];

const size_t URL_EXTRA_DATA_SIZE = 1u << 20;
typedef char urlextradata_t[URL_EXTRA_DATA_SIZE];

const size_t internal_doc_max = 10u << 20;
const size_t internal_doc_max_overhead = 2u << 10;

template <typename THeader>
void copy_header(THeader* to, const THeader* from) {
    *to = *from;
}

struct crc64_t {
    ui64 Crc;

    static const ui32 RecordSig = 0x12346001;

    crc64_t() {}
    crc64_t(ui64 h) : Crc(h) {}

    bool operator < (const crc64_t &b) const {
        return Crc < b.Crc;
    }
};

template <typename T1, typename T2>
int CompareUids(const T1* a, const T2* b) {
    int ret = ::compare(a->HostId, b->HostId);
    return ret ? ret : ::compare(a->UrlId, b->UrlId);
}

template <typename T1, typename T2>
int CompareUidsByRef(const T1& a, const T2& b) {
    int ret = ::compare(a.HostId, b.HostId);
    return ret ? ret : ::compare(a.UrlId, b.UrlId);
}

template <typename T1, typename T2>
int CompareSegmentDocId(const T1* a, const T2* b) {
    int ret = ::compare(a->SegmentId, b->SegmentId);
    return ret ? ret : ::compare(a->DocId, b->DocId);
}

template<typename Normal, typename FromSuffix>
int CompareUidFrom(const Normal* a, const FromSuffix* b) {
    int ret = ::compare(a->HostId, b->GetHostIdFrom());
    return ret ? ret : ::compare(a->UrlId, b->UrlIdFrom);
}

template<typename Normal, typename ToSuffix>
int CompareUidTo(const Normal* a, const ToSuffix* b) {
    int ret = ::compare(a->HostId, b->GetHostIdTo());
    return ret ? ret : ::compare(a->UrlId, b->UrlIdTo);
}

inline int CompareUidParts(ui32 HostId, ui64 UrlId, ui32 HostId2, ui64 UrlId2) {
    int res = ::compare(HostId, HostId2);
    if (res)
        return res;
    return ::compare(UrlId, UrlId2);
}

template <typename T1, typename T2>
void SetUid(T1* a, const T2* b) {
    a->HostId = b->HostId;
    a->UrlId = b->UrlId;
}

struct urlid_t {
    ui64      UrlId;
    ui32      HostId;

    static const ui32 RecordSig = 0x12346002;

    urlid_t() {}
    urlid_t(ui32 h, ui64 u) : UrlId(u), HostId(h) {}

    bool operator < (const urlid_t &b) const {
        return HostId < b.HostId || HostId == b.HostId && UrlId < b.UrlId;
    }

    bool operator == (const urlid_t &b) const {
        return HostId == b.HostId && UrlId == b.UrlId;
    }

    static int ByUid(const urlid_t *a, const urlid_t *b) {
        return ::CompareUids(a, b);
    }
};

MAKESORTERTMPL(urlid_t, ByUid);

struct TDbHandle {
    ui32 Handle;

    bool operator < (const TDbHandle &b) const {
        return Handle < b.Handle;
    }

    static int compare(const TDbHandle &a, const TDbHandle &b) {
        return ::compare(a.Handle, b.Handle);
    }
};

struct docid_t: public TDbHandle {
    static const ui32 RecordSig = 0x12346003;
    static const ui32 maxdoc    = ((ui32)1 << 31) - 1;
    static const ui32 maxgroup  = ~(ui32)0;
    static const ui32 invalid   = maxgroup;
    static const ui32 spammark  = maxdoc + 1;

    docid_t() {}
    docid_t(ui32 h) { Handle = h; }
    operator ui32 () const { return Handle; }

    static bool isdoc(ui32 doc) {
        return doc && doc <= maxdoc;
    }
    static bool issemidup(ui32 doc) {
        return doc > maxdoc;
    }
    static bool isspam(ui32 doc) {
        return doc & spammark;
    }
    static bool isinvalid(ui32 doc) {
        return doc == invalid;
    }
    static ui32 setspam(ui32 doc) {
        return doc | spammark;
    }
    static ui32 cleanspam(ui32 doc) {
        return doc & ~spammark;
    }

    static int ByDoc(const docid_t *a, const docid_t *b) {
        return ::compare(a->Handle, b->Handle);
    }
};

MAKESORTERTMPL(docid_t, ByDoc);

struct hostid_t: public TDbHandle {
    static const ui32 RecordSig = 0x12346004;
    hostid_t() {}
    hostid_t(ui32 h) { Handle = h; }
    operator ui32 () const { return Handle; }

    static int ByHost(const hostid_t *a, const hostid_t *b) {
        return ::compare(a->Handle, b->Handle);
    }
};

MAKESORTERTMPL(hostid_t, ByHost);

struct robots_old_t {
    ui32 Size;
    char Data[robots_max];
};

struct multirobots_t {
    ui32 Size;
    char Data[robots_max];

    bool IsEmpty() const {
        if (Size == 0)
            return true;
        const char* ptr = Data;
        ui32 numOfBots = *((ui32*)ptr);
        return Size <= sizeof(ui32) * (1 + 2 * numOfBots);
    }
};

struct host_robots_old_t {
    ui32 Size;
    char Data[robots_max];
};

struct host_multirobots_t {
    ui32 Size;
    char Data[robots_max];

    // @len is size of original buffer with the structure
    // it's assumed that buffer has terminating \0 (like in TString) even for invalid structures
    bool IsValid(const size_t len) const {
        // standart YT table row size is 16 MB, allow only 14 MB taking into account other fields
        // @see JUPITER-1752 for details
        if (len > 14_MB) {
            return false;
        }

        if (len < sizeof(ui32)) {
            return false;
        }
        return len == FullSize();
    }

    bool IsEmpty() const {
        if (Size <= sizeof(ui32)) { // numOfBots is zero or out of record
            return true;
        }
        size_t namelen = strlen(Data) + 1;
        const char* ptr = Data + namelen;
        ui32 numOfBots = *((ui32*)ptr);
        return Size <= sizeof(ui32) * (1 + 2 * numOfBots);
    }

    bool GetRobots(const char*& robots) const {
        if (IsEmpty()) {
            return false;
        }
        robots = Data + strlen(Data) + 1;
        return true;
    }

    ui32 FullSize() const {
        return Size + 1 + strlen(Data) + sizeof(ui32);
    }
};

const size_t cstream_max = internal_doc_max;
const size_t cstream_max_zip = ((ui64)cstream_max - 12) * 1000 / 1001;

struct cstream_t {
    ui32 Size;
    char Data[cstream_max];
};

struct urlstream_t {
    ui32 CSize;
    char Data[cstream_max + URL_MAX];
};

//please, support consistency of TStreamElemTypeTPrinter with stream_elem_type_t
enum stream_elem_type_t {
    SET_URL_NAME = 1,
    SET_ORIG_DOC = 2,
    SET_HTML_CONV = 3,
    SET_URLS = 4,
    SET_EXTLINKS = 5,
    SET_RANGE_INFO = 6,
    SET_RANGE_BODY = 7,
    SET_LINKTEXT = 8,   // contains protobuf TAnchorText declared in yweb/protos/robot.proto
    SET_LANGUAGES = 9,  // contains additional languages with their wheights
    SET_MAX
};
// converts stream_elem_type_t to text
class TStreamElemTypeTPrinter {
public:
    TStreamElemTypeTPrinter(stream_elem_type_t const v)
        : Value(v)
    {}

    char const* operator~() {
        if (Value == SET_URL_NAME) {
            return "SET_URL_NAME";
        } else if (Value == SET_ORIG_DOC) {
            return "SET_ORIG_DOC";
        } else if (Value == SET_HTML_CONV) {
            return "SET_HTML_CONV";
        } else if (Value == SET_URLS) {
            return "SET_URLS";
        } else if (Value == SET_EXTLINKS) {
            return "SET_EXTLINKS";
        } else if (Value == SET_RANGE_INFO) {
            return "SET_RANGE_INFO";
        } else if (Value == SET_RANGE_BODY) {
            return "SET_RANGE_BODY";
        } else if (Value == SET_LINKTEXT) {
            return "SET_LINKTEXT";
        } else if (Value == SET_LANGUAGES) {
            return "SET_LANGUAGES";
        }

        return "unknown";
    }

protected:
    stream_elem_type_t Value;
};


struct stream_elem_t {
    stream_elem_type_t  type;
    ui32                size;
    char                data[];
};

struct TDynamicCharBuffer {
    static const ui32 MAX_DYNAMIC_CHAR_BUFFER_SIZE = (32<<10) - 8;

    ui32 Size;
    char Data[MAX_DYNAMIC_CHAR_BUFFER_SIZE];

    TDynamicCharBuffer() {}

    TDynamicCharBuffer(const char* data, size_t len) {
        Set(data, len);
    }

    static size_t OffsetOfData() {
        static TDynamicCharBuffer rec;
        return rec.OffsetOfDataImpl();
    }

    void SetUnchecked(const void *data, size_t len) {
        Size = (ui32)len;
        memcpy(Data, data, Size);
    }

    void Set(const void *data, size_t len) {
        if (len >= MAX_DYNAMIC_CHAR_BUFFER_SIZE) {
            ythrow yexception() << "TDynamicCharBuffer: buffer size exceed";
        }
        SetUnchecked(data, len);
    }

    void Reset() {
        Size = 0;
    }
private:
    size_t OffsetOfDataImpl() const {
        return offsetofthis(Data);
    }
};

struct TStringStringMapBuffer: public TDynamicCharBuffer {
    // Data field must be readable by ::LoadMap<THashMap<String, String>>(...)
};

struct packed_stream_t {
    ui32 TotalSize;
    // the structure is of variable size;
    // variable length data is available via call to Data()

    char *Data() {
        return (char *)this + sizeof(packed_stream_t);
    }

    const char *Data() const {
        return (const char *)this + sizeof(packed_stream_t);
    }

    size_t SizeOf() const {
        return sizeof(packed_stream_t) + TotalSize;
    }

    struct const_iterator {
        const stream_elem_t *ptr;
        const_iterator(const stream_elem_t *src = nullptr) : ptr(src) {}
        const stream_elem_t *operator ->() { return ptr; }
        const stream_elem_t &operator*() { return *ptr; }
        const_iterator operator ++() {
            ptr = (const stream_elem_t *)((char const*)ptr + sizeof(stream_elem_t) + DatCeil(ptr->size));
            return *this;
        }
        const_iterator operator ++(int) {
            const_iterator tmp = *this;
            operator++();
            return tmp;
        }
        bool operator ==(const const_iterator &it) const { return ptr == it.ptr; }
        bool operator !=(const const_iterator &it) const { return !operator==(it); }
    };

    const_iterator begin() const { return const_iterator((const stream_elem_t*)Data()); }
    const_iterator end() const { return const_iterator((const stream_elem_t*)DatCeil((size_t)Data() + TotalSize)); }

    /*
     * Use case: write to packed_stream_t
     * CalcTotalSize() -> allocate memory -> BeforePushing() -> Push() -> ...
     */
    static size_t CalcTotalSize(unsigned itemsNum, size_t datceiledItemsSize) {
        return offsetof(stream_elem_t, data) * itemsNum + datceiledItemsSize;
    }

    void BeforePushing() {
        TotalSize = 0;
    }

    void Push(stream_elem_type_t type, const void *data, size_t size) {
        Y_ASSERT((TotalSize & 0x3) == 0);
        stream_elem_t *elem = (stream_elem_t *)(Data() + TotalSize);
        elem->type = type;
        elem->size = (ui32)size;
        DatSet(elem->data, size);
        memcpy(elem->data, data, size);
        TotalSize += ui32(offsetof(stream_elem_t, data) + DatCeil(size));
    }

    const stream_elem_t *Find(stream_elem_type_t type) const {
        for (const_iterator it = begin(); it != end(); ++it)
            if (it->type == type)
                return &(*it);
        return nullptr;
    }
};

// seconds since epoch
class TDataworkDate : public TAutoZero<i32> {
    typedef TAutoZero<i32> TBase;
    typedef i32 TDate;
public:
    inline explicit TDataworkDate(TDate date = 0)
        : TBase(date)
    { }
    TDataworkDate& operator=(const TDate& date) {
        TBase::Value_ = date;
        return *this;
    }
};

template <>
inline void Out<TDataworkDate>(IOutputStream& os, TTypeTraits<TDataworkDate>::TFuncParam date) {
    Out<i32>(os, (ui32)date);
}


struct TDocumentFlags {
    enum {
        // Microformat flags
        Article = 0x1,
        Recipe = 0x2,
        Card = 0x4,
        CardOrg = 0x8,
        Sorg = 0x10,
        TermDef = 0x20,
        Ogp = 0x40,
        Hreview = 0x80,

        // Video flags
        Video = 0x100,

        // yandex direct flags
        Context = 0x10000,
        Metric = 0x20000
    };

    TDocumentFlags() {
        Init();
    }

    void Init() {
        Zero(*this);
    }

    bool Empty() const {
        return !Flags;
    }

    void SetFlags(ui32 mask, bool flag) {
        if (flag) {
            Flags |= mask;
        } else {
            Flags &= ~mask;
        }
    }

#define SET_GET_METHOD(_FLAG_) \
    void Set##_FLAG_(bool flag) { \
        SetFlags(_FLAG_, flag); \
    } \
    bool Get##_FLAG_() const { \
        return Flags & _FLAG_; \
    }

    SET_GET_METHOD(Article)
    SET_GET_METHOD(TermDef)
    SET_GET_METHOD(Recipe)
    SET_GET_METHOD(Card)
    SET_GET_METHOD(CardOrg)
    SET_GET_METHOD(Sorg)
    SET_GET_METHOD(Ogp)
    SET_GET_METHOD(Hreview)

    SET_GET_METHOD(Video)

    SET_GET_METHOD(Context)
    SET_GET_METHOD(Metric)

#undef SET_GET_METHOD

#define FLAG_TO_STRING_ITEM(_FLAG_) \
    if (flags & (_FLAG_)) result += TString((!result.empty())?"|":"") + #_FLAG_

    static TString ToString(ui32 flags) {
        TString result;
        FLAG_TO_STRING_ITEM(Article);
        FLAG_TO_STRING_ITEM(TermDef);
        FLAG_TO_STRING_ITEM(Recipe);
        FLAG_TO_STRING_ITEM(Card);
        FLAG_TO_STRING_ITEM(CardOrg);
        FLAG_TO_STRING_ITEM(Sorg);
        FLAG_TO_STRING_ITEM(Ogp);
        FLAG_TO_STRING_ITEM(Hreview);
        FLAG_TO_STRING_ITEM(Video);
        FLAG_TO_STRING_ITEM(Context);
        FLAG_TO_STRING_ITEM(Metric);
        return result;
    }

#undef FLAG_TO_STRING_ITEM

public:
    ui32 Flags;
};


struct TUrlVersion {
    typedef ui64 TUrlId;
    typedef ui32 TVersion;

    TUrlId   UrlId;
    TVersion Version;

    static const ui32 RecordSig = 0x12346005;

    TUrlVersion(const TUrlId urlId, const TVersion version = 0)
        : UrlId(urlId)
        , Version(version) {}

    bool operator < (const TUrlVersion &b) const {
        return UrlId < b.UrlId;
    }
};

#pragma pack(pop)
