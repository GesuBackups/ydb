#pragma once

#include <util/digest/fnv.h>
#include <util/generic/yexception.h>
#include <util/generic/vector.h>

#include "dbtypes.h"

#include <array>

// fix GCC bug http://gcc.gnu.org/bugzilla/show_bug.cgi?id=7046
inline void yweb_robot_dbsheeme_shinglerecords_h() {
    TVector<ui32> tmp;
}

#pragma pack(push, 4)

const size_t SHINGLE_SIZE = 25;

struct TShingle : std::array<ui32, SHINGLE_SIZE>
{
    TShingle() = default;

    TShingle(const TShingle& shingle)
    {
        for (size_t i = 0; i < SHINGLE_SIZE; ++i)
            data()[i] = shingle.data()[i];
    }

    void operator=(const TVector<ui32>& shingle)
    {
        if (shingle.size() != SHINGLE_SIZE)
            ythrow yexception() << "Invalid shingle size";
        for (size_t i = 0; i < SHINGLE_SIZE; ++i)
            data()[i] = shingle[i];
    }

    void operator=(const TShingle& shingle)
    {
        for (size_t i = 0; i < SHINGLE_SIZE; ++i)
            data()[i] = shingle.data()[i];
    }

    void Clear()
    {
        memset(data(), 0, sizeof(TShingle));
    }

    int Incorrect() const
    {
        for (size_t i = 1; i < SHINGLE_SIZE; ++i)
            if (data()[i - 1] > data()[i])
                return (int)i;
        return 0;
    }

    bool IsEmpty() const
    {
        return !data()[SHINGLE_SIZE - 1];
    }
};

Y_DECLARE_PODTYPE(TShingle);

template <class A, class B>
static int CompareShingleId(const A* a, const B* b)
{
    if (a->HostId != b->HostId)
        return ::compare(a->HostId, b->HostId);
    return ::compare(a->ShingleId, b->ShingleId);
}

struct TShingleIdRec
{
    ui32 HostId;
    ui64 ShingleId;
    TShingle Shingle;

    static const ui32 RecordSig = 0x55891311;

    operator urlid_t() const
    {
        return urlid_t(HostId, ShingleId);
    }

    static int ByShingleId(const TShingleIdRec* a, const TShingleIdRec* b)
    {
        return CompareShingleId<TShingleIdRec, TShingleIdRec>(a, b);
    }

    static ui64 CountShingleId(const TShingle* shingle)
    {
        return FnvHash<ui64>(shingle, sizeof(TShingle));
    }
};

MAKESORTERTMPL(TShingleIdRec, ByShingleId);

class TDelShingle : public TShingleIdRec
{
public:
    TDelShingle()
    {
        memset(&Shingle, 0xFF, sizeof(TShingle));
        ShingleId = CountShingleId(&Shingle);
    }
};

struct TShingleUrlRec : public urlid_t
{
    ui64 ShingleId;
    TDataworkDate AddTime;

    static const ui32 RecordSig = 0x15891302;

    static int ByShingleId(const TShingleUrlRec* a, const TShingleUrlRec* b)
    {
        return CompareShingleId<TShingleUrlRec, TShingleUrlRec>(a, b);
    }

    static int ByUidAndTime(const TShingleUrlRec* a, const TShingleUrlRec* b)
    {
        int c = ByUid(a, b);
        if (c) return c;
        return ::compare(b->AddTime, a->AddTime);
    }
};

MAKESORTERTMPL(TShingleUrlRec, ByUid);
MAKESORTERTMPL(TShingleUrlRec, ByShingleId);
MAKESORTERTMPL(TShingleUrlRec, ByUidAndTime);

class TUniqShingleIdSieve
{
public:
    template<class TShingleRec>
    static int Sieve(TShingleRec *a, const TShingleRec *b)
    {
        if (a == b || a->HostId != b->HostId)
            return 0;
        return a->ShingleId == b->ShingleId;
    }
};

const int TOP_DOCID_SHINGLES_COUNT = 3;
struct TDocIdShinglesRec: public docid_t {
    std::array<ui32, TOP_DOCID_SHINGLES_COUNT> Shingles;

    TDocIdShinglesRec(docid_t docId, const ui32* begin, const ui32* end)
        : docid_t(docId)
    {
        Shingles.fill((ui32)-1);
        std::copy(begin, end, Shingles.begin());
    }

    static int ByDocId(const TDocIdShinglesRec* a, const TDocIdShinglesRec* b)
    {
         return ::compare((ui32)*a, (ui32)*b);
    }
};

MAKESORTERTMPL(TDocIdShinglesRec, ByDocId);

struct TShingleLogel
{
    TShingle Shingle;
    TDataworkDate AddTime;
    url_t FullUrl;

    static const ui32 RecordSig = 0x55891351;

    static size_t OffsetOfFullUrl() {
        static TShingleLogel rec;
        return rec.OffsetOfFullUrlImpl();
    }

    size_t SizeOf() const {
        return offsetofthis(FullUrl) + strlen(FullUrl) + 1;
    }

private:
    size_t OffsetOfFullUrlImpl() const {
        return offsetofthis(FullUrl);
    }

};

#pragma pack(pop)
