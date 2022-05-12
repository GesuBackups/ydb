#pragma once
#include <ydb/core/base/defs.h>
#include <ydb/core/base/events.h>
#include <ydb/core/base/ticket_parser.h>

namespace NKikimr {
    struct TEvBlackBoxValidator {
        enum EEv {
            // requests
            EvValidateTicket = EventSpaceBegin(TKikimrEvents::ES_BLACKBOX_VALIDATOR),

            // replies
            EvValidateTicketResult = EvValidateTicket + 512,

            EvEnd
        };

        static_assert(EvEnd < EventSpaceEnd(TKikimrEvents::ES_BLACKBOX_VALIDATOR), "expect EvEnd < EventSpaceEnd(TKikimrEvents::ES_BLACKBOX_VALIDATOR)");

        struct TEvValidateTicket : TEventLocal<TEvValidateTicket, EvValidateTicket> {
            TString Key;
            TString Ticket;

            TEvValidateTicket(const TString& key, const TString& ticket)
                : Key(key)
                , Ticket(ticket)
            {}
        };

        using TError = TEvTicketParser::TError;

        struct TEvValidateTicketResult : TEventLocal<TEvValidateTicketResult, EvValidateTicketResult> {
            TString Key;
            TError Error;
            ui64 UID;

            TEvValidateTicketResult(const TString& key, ui64 uid)
                : Key(key)
                , UID(uid)
            {}

            TEvValidateTicketResult(const TString& key, const TError& error)
                : Key(key)
                , Error(error)
            {}
        };

    };

    IActor* CreateBlackBoxValidator(const TString& host = "blackbox.yandex-team.ru");
}

