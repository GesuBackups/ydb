#pragma once

#include "hide.h"

#include <util/system/defaults.h>
#include <util/generic/string.h>
#include <library/cpp/deprecated/datafile/loadmode.h>

enum UrlFilter {
    //0-8 are THttpUrl::TParsedState
    UrlFilterOK = 0,
    UrlFilterEmpty,
    UrlFilterOpaque,
    UrlFilterBadFormat,
    UrlFilterBadPath,
    UrlFilterTooLong,
    UrlFilterBadPort,
    UrlFilterBadAuth,
    UrlFilterBadScheme,
    UrlFilterHost = 9,
    UrlFilterUrl,
    UrlFilterSuffix,
    UrlFilterDomain,
    UrlFilterExtDomain,
    UrlFilterPort,
    UrlFilterHashVal,
    UrlFilterMax
};

class TUrlFilterData; // urlfilter_int.h

class TUrlFilter : THiddenImpl<TUrlFilterData, sizeof(size_t) * 16> {
public:
    TUrlFilter();
    ~TUrlFilter();

    void Load(const char *fname, EDataLoadMode loadMode = DLM_READ);
    int Reject(const char *Url) const;
    int Reject2(const char *Host, const char *Path) const;
    int RejectHost(const char *Host) const {
        return Reject2(Host, nullptr);
    }
    bool IsGoodExtension(const char* path) const;

    void PrintNode(const ui32 *Node, int ident, bool friendly, const TString &me) const;
    void PrintRootNode(bool friendly) const;
    void Precharge() const; // for DLM_MAP* only
};

int FilterReject(const char *Url);
int FilterReject2(const char *Host, const char *Path);
int FilterRejectHost(const char *Host);
int FilterLoad(const char *fname);
int isGoodExtension(const char* path);

void FilterSwap(); // hackish way to have two filters
