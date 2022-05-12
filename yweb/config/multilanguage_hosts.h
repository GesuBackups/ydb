#pragma once

#include "langregion.h"

#include <kernel/multilanguage_hosts/multilanguage_hosts.h>

#include <util/generic/hash.h>
#include <util/stream/file.h>
#include <util/system/maxlen.h>
#include <util/ysaveload.h>

inline TString EncodeMultilanguageHost(const ELangRegion langRegion, const TString& host) {
    return EncodeMultilanguageHost(langRegion, host, GetDefaultLangRegion());
}

inline void LoadGluedLinksHosts(const char* name, THashMap<ui32, TVector<ui32> >& hostIdToGluedHostIds) {
    TUnbufferedFileInput gluedLinksHostsFile(name);
    TVector< std::pair<ui32, ui32> > gluedLinksHosts;
    Load(&gluedLinksHostsFile, gluedLinksHosts);
    for (ui32 i = 0; i < gluedLinksHosts.size(); ++i)
        hostIdToGluedHostIds[gluedLinksHosts[i].first].push_back(gluedLinksHosts[i].second);
}

inline void LoadGluedLinksHosts(const char* name, THashMap<ui32, ui32>& mainGluedHostIds) {
    TUnbufferedFileInput gluedLinksHostsFile(name);
    TVector< std::pair<ui32, ui32> > gluedLinksHosts;
    Load(&gluedLinksHostsFile, gluedLinksHosts);
    for (ui32 i = 0; i < gluedLinksHosts.size(); ++i)
        if (gluedLinksHosts[i].second != gluedLinksHosts[i].first)
            mainGluedHostIds[gluedLinksHosts[i].second] = gluedLinksHosts[i].first;
}
