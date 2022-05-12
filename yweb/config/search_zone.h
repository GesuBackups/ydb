#pragma once

#include <library/cpp/charset/doccodes.h>

#include <util/generic/algorithm.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NZones {

/////////////////////////
/// search zone codes ///
/////////////////////////

enum ESearchZoneCode {
    ALLLAST_SEARCH // 0
    ,WEBTIER_SEARCH // 1
    ,PLATINUM_SEARCH // 2
    ,DIVERSITY_SEARCH // 3
    ,RUS_MAPS_SEARCH // 4
    ,TUR_MAPS_SEARCH // 5
    ,DIVERSITY_TR_SEARCH // 6
    ,WEB_MUSOR_SEARCH // 7
    ,SEARCH_ZONES_COUNT // 8
};

static const ui8 SEARCH_ZONES_NUM = SEARCH_ZONES_COUNT;

class TSearchZone {

public:

    inline TSearchZone(const char* name, const ESearchZoneCode code, const bool isUploadZone);

    inline const char* GetName() const {
        return Name.data();
    }

    inline ESearchZoneCode GetCode() const {
        return Code;
    }

    inline bool IsUpload() const {
        return IsUploadZone;
    }

    static ESearchZoneCode Parse(const TString& searchZoneName);
    static const char* GetNameByCode(ESearchZoneCode code);
    static const TSearchZone* GetZoneByCode(ESearchZoneCode code);

private:

    const TString Name;
    const ESearchZoneCode Code;
    const bool IsUploadZone;
};


} //namespace
