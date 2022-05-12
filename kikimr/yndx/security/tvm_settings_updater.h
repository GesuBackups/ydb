#pragma once

#include <ydb/core/base/defs.h>
#include <ydb/core/base/events.h>

namespace NKikimr {
    struct TEvTVMSettingsUpdater {
        enum EEv {
            EvUpdateTVMPublicKeys = EventSpaceBegin(TKikimrEvents::ES_TVM_SETTINGS_UPDATER),
            EvUpdateTVMSettings,

            EvEnd
        };

        static_assert(EvEnd < EventSpaceEnd(TKikimrEvents::ES_TVM_SETTINGS_UPDATER), "expect EvEnd < EventSpaceEnd(TKikimrEvents::ES_TVM_SETTINGS_UPDATER)");

        struct TEvUpdateTVMPublicKeys: TEventLocal<TEvUpdateTVMPublicKeys, EvUpdateTVMPublicKeys> {};

        struct TEvUpdateTVMSettings: TEventLocal<TEvUpdateTVMSettings, EvUpdateTVMSettings> {
            const ui32 ServiceTVMId;
            const TString PublicKeys;

            TEvUpdateTVMSettings(const ui32 serviceTVMId, const TString& publicKeys)
                : ServiceTVMId(serviceTVMId)
                , PublicKeys(publicKeys)
            {}
        };
    };

    inline NActors::TActorId MakeTVMSettingsUpdaterID() {
        const char name[11] = "tvmupdater";
        return NActors::TActorId(0, TStringBuf(name, 11));
    }

    IActor* CreateTVMSettingsUpdater(const TActorId& ticketParser, const ui64 serviceTVMId, const TString& initialPublicKeys, const bool updatePublicKeys, const ui64 updatePublicKeysSuccessTimeout, const ui64 updatePublicKeysFailureTimeout);
}
