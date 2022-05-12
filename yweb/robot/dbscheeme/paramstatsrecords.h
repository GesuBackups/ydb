#pragma once

#include <stddef.h>
#include <string.h>
#include <util/system/compat.h>
#include <util/system/defaults.h>

#include <library/cpp/microbdb/sorterdef.h>
#include "dbtypes.h"

#include <array>

const int PARAM_MAX = 32;
struct TParamName : std::array<char,PARAM_MAX> {
    TParamName() { }
    TParamName(const char* paramName, size_t len) {
        Set(paramName, len);
    }
/*
    void Set(const char* paramName) {
        if (paramName)
            strncpy(this->data(), paramName, this->size());
        else
            memset(this->data(), 0, this->size());
    }
*/
    void Set(const char* paramName, size_t len) {
        if (len >= this->size())
            ythrow yexception() << "TParamName::Set: len of param '" <<  paramName << "' is too long (" <<  (ui64)len << ")";
        memcpy(this->data(), paramName, len);
        memset(this->data() + len, 0, this->size() - len);
    }
    void Reset() {
        memset(this->data(), 0, this->size());
    }
};

typedef ui32 TShortCount;
typedef ui64 TLongCount;
typedef ui64 TFnvPath;

#pragma pack(push, 4)

struct TParamCountSectionId {
    ui32 HostId;
    TFnvPath FnvPath;

    static const ui32 RecordSig = 0x678FA63F;
    bool operator<(const TParamCountSectionId& b) const {
        return HostId < b.HostId || HostId == b.HostId && FnvPath < b.FnvPath;
    }
    bool operator==(const TParamCountSectionId& b) const {
        return HostId == b.HostId && FnvPath == b.FnvPath;
    }
};

inline int compare(const TParamCountSectionId &a, const TParamCountSectionId &b) {
    int r = compare(a.HostId, b.HostId);
    if (!r)
        r = compare(a.FnvPath, b.FnvPath);
    return r;
}

inline int compare(const TParamName &a, const TParamName &b) {
    return strncmp(a.data(), b.data(), PARAM_MAX);
}

struct TParamCountGlobalRec {
    TLongCount Count;
    TLongCount RealCount;
    TShortCount UnknownCount;
    TShortCount SignifCount;
    TShortCount SemiSignifCount;
    TShortCount InsignifCount;
    TParamName ParamName;
    static const ui32 RecordSig = 0x678FA63E;

    static int ByDefault(const TParamCountGlobalRec *a, const TParamCountGlobalRec *b) {
        return compare(a->ParamName, b->ParamName);
    }
    void Reset() {
        memset(this, 0, sizeof(*this));
    }
};

MAKESORTERTMPL(TParamCountGlobalRec, ByDefault);

struct TParamCountRec
    : public TParamCountSectionId
    , public TParamCountGlobalRec
{
    static const ui32 RecordSig = 0x678FA63B;

    static int ByDefault(const TParamCountRec *a, const TParamCountRec *b) {
        int r = compare(*(TParamCountSectionId*)a, *(TParamCountSectionId*)b);
        if (!r)
            r = compare(a->ParamName, b->ParamName);
        return r;
    }
    void Reset() {
        memset(this, 0, sizeof(*this));
    }

    void operator+=(const TParamCountRec &o) {
        Count += o.Count;
        RealCount += o.RealCount;
        UnknownCount += o.UnknownCount;
        SignifCount += o.SignifCount;
        SemiSignifCount += o.SemiSignifCount;
        InsignifCount += o.InsignifCount;
    }
};

MAKESORTERTMPL(TParamCountRec, ByDefault);

struct TUrlPathRec
    : public TParamCountSectionId
{
    TShortCount UrlCount;
    TShortCount DocCount;
    TShortCount GroupCount;
    url_t UrlPath;

    static const ui32 RecordSig = 0x678FA63D;
    size_t SizeOf() const {
        return strchr(UrlPath, 0) + 1 - (char*)(void*)this;
    }
    static int BySectionId(const TUrlPathRec *a, const TUrlPathRec *b) {
        return compare(*(TParamCountSectionId*)a, *(TParamCountSectionId*)b);
    }
    static int ByDefault(const TUrlPathRec *a, const TUrlPathRec *b) {
        int r = compare(*(TParamCountSectionId*)a, *(TParamCountSectionId*)b);
        if (!r)
            // this is to make empty paths go last
            r = -compare(a->UrlPath[0], b->UrlPath[0]);
        return r;
    }
    void Reset() {
        UrlPath[0] = 0;
        memset(this, 0, SizeOf());
    }
};

MAKESORTERTMPL(TUrlPathRec, ByDefault);
MAKESORTERTMPL(TUrlPathRec, BySectionId);

struct TCleanParamRec
{
    dbscheeme::host_t HostName;
    url_t UrlPath;
    TParamName ParamName;
    TDataworkDate Added;

    static const ui32 RecordSig = 0x678FA640;
    void Reset() {
        memset(this, 0, sizeof(*this));
    }
    void SetHostName(const char* hostName) {
        strncpy(HostName, hostName, sizeof(dbscheeme::host_t));
    }
    void SetUrlPath(const char *urlPath) {
        strncpy(UrlPath, urlPath, sizeof(url_t));
    }
    void SetParamName(const char* paramName) {
        strncpy(ParamName.data(), paramName, sizeof(ParamName));
    }
};

#pragma pack(pop)

