#pragma once

#include "dbtypes.h"

#include <array>

#pragma pack(push, 4)

const unsigned short  DUMPMAX = 3000;
const unsigned char SSIZE = sizeof(short);

struct TDataDump : std::array<char, 3 * DUMPMAX>  {
    TDataDump() {
        Reset();
    }
    TDataDump(const char* data, size_t len) {
        Set(data, len);
    }
    void Set(const char* data, size_t len) {
        Reset();
        Add(data, len);
    }
    void Add(const char* data, size_t len) {
        unsigned short curL = Size() + SSIZE;
        if ( len + curL + SSIZE >= this->size() )
            throw yexception() << "TDataDump::Set: len of param '" << data << "' is too long (" << (int)len << ")";
        memcpy(this->data() + curL + SSIZE, data, len);
        Size(curL) = (unsigned short)len;
        Size() = curL + (unsigned short)len;
        Count()++;
    }

    void Reset() {
        Size() = 1;
        Count() = 0;
    }
    int GetElement(unsigned char n, char *&data) const {
        if (n >= Count())
            ythrow yexception() << "Trying to get element " << n << " but stored only " << Count() << " elements.\n";
        unsigned short pos = SSIZE + 1;
        for(size_t i = 0; i < n; i++)
            pos += Size(pos) + SSIZE;
        data = const_cast<char*>(this->data() + pos + SSIZE);
        return  Size(pos);
    }
    unsigned short& Size(int pos = 0) const {
        return *(unsigned short*)(this->data() + pos);
    }
    unsigned char& Count() const {
        return *(unsigned char*)(this->data() + SSIZE);
    }
};

struct TClassifRec {
    ui64         DocId;
    ui16         ClasMethod;
    TDataDump    ClasRes;
    static const ui32 RecordSig = 0x89746116;
    TClassifRec()
        : DocId(0)
        , ClasMethod(0) {
    }
    size_t SizeOf() const {
        return offsetof(TClassifRec, ClasRes) + SSIZE + ClasRes.Size();
    }

    static int ByDocId(const TClassifRec *a, const TClassifRec *b) {
        return compare(a->DocId, b->DocId);
    }
    static int ByClasMethod(const TClassifRec *a, const TClassifRec *b) {
        return compare(a->ClasMethod, b->ClasMethod);
    }
};

MAKESORTERTMPL(TClassifRec, ByDocId);
MAKESORTERTMPL(TClassifRec, ByClasMethod);

#pragma pack(pop)
