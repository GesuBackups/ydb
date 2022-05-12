#include <util/generic/yexception.h>

#include "search_zone.h"

namespace NZones {

static const TSearchZone WEB_SEARCH_ZONE = TSearchZone("WebTier0", WEBTIER_SEARCH, true);
static const TSearchZone PLATINUM_SEARCH_ZONE = TSearchZone("PlatinumTier0", PLATINUM_SEARCH, true);
static const TSearchZone DIVERSITY_SEARCH_ZONE = TSearchZone("Div0", DIVERSITY_SEARCH, true);
static const TSearchZone RUS_MAPS_SEARCH_ZONE = TSearchZone("RusMaps0", RUS_MAPS_SEARCH, true);
static const TSearchZone TUR_MAPS_SEARCH_ZONE = TSearchZone("TurMaps0", TUR_MAPS_SEARCH, true);
static const TSearchZone DIVERSITY_TR_SEARCH_ZONE = TSearchZone("DivTr0", DIVERSITY_TR_SEARCH, true);
static const TSearchZone WEB_MUSOR_SEARCH_ZONE = TSearchZone("WebTier1", WEB_MUSOR_SEARCH, true);

static const TSearchZone ALLLAST_SEARCH_ZONE = TSearchZone("AllL", ALLLAST_SEARCH, false);

const TSearchZone* SEARCH_ZONE_BYCODE[SEARCH_ZONES_NUM] = {
    &ALLLAST_SEARCH_ZONE,
    &WEB_SEARCH_ZONE,
    &PLATINUM_SEARCH_ZONE,
    &DIVERSITY_SEARCH_ZONE,
    &RUS_MAPS_SEARCH_ZONE,
    &TUR_MAPS_SEARCH_ZONE,
    &DIVERSITY_TR_SEARCH_ZONE,
    &WEB_MUSOR_SEARCH_ZONE
};

TSearchZone::TSearchZone(const char* name, const ESearchZoneCode code, const bool isUploadZone)
    : Name(name)
    , Code(code)
    , IsUploadZone(isUploadZone) {}

ESearchZoneCode TSearchZone::Parse(const TString& searchZoneName) {
    if (searchZoneName == WEB_SEARCH_ZONE.GetName()) {
        return WEB_SEARCH_ZONE.GetCode();
    } else if (searchZoneName == PLATINUM_SEARCH_ZONE.GetName()) {
        return PLATINUM_SEARCH_ZONE.GetCode();
    } else if (searchZoneName == DIVERSITY_SEARCH_ZONE.GetName()) {
        return DIVERSITY_SEARCH_ZONE.GetCode();
    } else if (searchZoneName == RUS_MAPS_SEARCH_ZONE.GetName()) {
        return RUS_MAPS_SEARCH_ZONE.GetCode();
    } else if (searchZoneName == TUR_MAPS_SEARCH_ZONE.GetName()) {
        return TUR_MAPS_SEARCH_ZONE.GetCode();
    } else if (searchZoneName == DIVERSITY_TR_SEARCH_ZONE.GetName()) {
        return DIVERSITY_TR_SEARCH_ZONE.GetCode();
    } else if (searchZoneName == WEB_MUSOR_SEARCH_ZONE.GetName()) {
        return WEB_MUSOR_SEARCH_ZONE.GetCode();
    } else if (searchZoneName == ALLLAST_SEARCH_ZONE.GetName()) {
        return ALLLAST_SEARCH_ZONE.GetCode();
    } else {
        ythrow yexception() << "Unknown search zone name: " << searchZoneName;
    }
}

const char* TSearchZone::GetNameByCode(ESearchZoneCode code) {
    ui32 index = static_cast<ui32>(code);
    if (index >= SEARCH_ZONES_NUM)
        ythrow yexception() << "Unknown search zone code: " << (ui32)code;
    return SEARCH_ZONE_BYCODE[index]->GetName();
}

const TSearchZone* TSearchZone::GetZoneByCode(ESearchZoneCode code) {
    ui32 index = static_cast<ui32>(code);
    if (index >= SEARCH_ZONES_NUM)
        ythrow yexception() << "Unknown search zone code: " << (ui32)code;
    return SEARCH_ZONE_BYCODE[index];
}

} //namespace
