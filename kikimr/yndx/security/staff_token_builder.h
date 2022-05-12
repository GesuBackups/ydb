#pragma once
#include <ydb/core/base/defs.h>
#include <ydb/core/base/events.h>
#include <ydb/core/base/ticket_parser.h>
#include <ydb/library/aclib/aclib.h>

namespace NKikimr {
    struct TEvStaffTokenBuilder {
        enum EEv {
            // requests
            EvBuildToken = EventSpaceBegin(TKikimrEvents::ES_TOKEN_BUILDER),

            // replies
            EvBuildTokenResult = EvBuildToken + 512,

            EvEnd
        };

        static_assert(EvEnd < EventSpaceEnd(TKikimrEvents::ES_TOKEN_BUILDER), "expect EvEnd < EventSpaceEnd(TKikimrEvents::ES_TOKEN_BUILDER)");

        struct TEvBuildToken : TEventLocal<TEvBuildToken, EvBuildToken> {
            TString Key;
            ui64 UID = {};
            TString Login;

            TEvBuildToken(const TString& key, ui64 uid)
                : Key(key)
                , UID(uid)
            {}

            TEvBuildToken(const TString& key, const TString& login)
                : Key(key)
                , Login(login)
            {}
        };

        using TError = TEvTicketParser::TError;

        struct TEvBuildTokenResult : TEventLocal<TEvBuildTokenResult, EvBuildTokenResult> {
            TString Key;
            TError Error;
            TIntrusivePtr<NACLib::TUserToken> Token;

            TEvBuildTokenResult(const TString& key, NACLib::TUserToken* token)
                : Key(key)
                , Token(token)
            {}

            TEvBuildTokenResult(const TString& key, const TError& error)
                : Key(key)
                , Error(error)
            {}
        };

    };

    IActor* CreateStaffTokenBuilder(const TString& token, const TString& host = "staff-api.yandex-team.ru", const TString& domain = "staff");
}

