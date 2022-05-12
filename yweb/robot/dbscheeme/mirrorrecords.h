#pragma once

#include <util/system/defaults.h>
#include "dbtypes.h"

#pragma pack(push, 4)

#if defined (_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable:4706) /*assignment within conditional expression*/
#   pragma warning(disable:4267) /*conversion from 'size_t' to 'type', possible loss of data*/
#endif

struct TMirrorHostRec {
    ui32 Status;
    dbscheeme::host_t Name;

    static const ui32 RecordSig = 0x12341405;

    TMirrorHostRec(const char* name, ui32 status = 0)
         : Status(status)
    {
        strlcpy(Name, name, sizeof(Name));
    }

    size_t SizeOf() const {
        return offsetof(TMirrorHostRec, Name) + strlen(Name) + 1;
    }
};

struct TMirrorHostLogel {
    ui32 Status;
    ui16 Port;
    ui16 Dummy;
    ui32 Ip;
    ui8  Lang;
    dbscheeme::host_t Name;

    static const ui32 RecordSig = 0x12351406;

    TMirrorHostLogel(const char* name, ui16 port, ui32 status, ui32 ip, ui8 lang)
        : Status(status)
        , Port(port)
        , Dummy(0)
        , Ip(ip)
        , Lang(lang)
    {
        strlcpy(Name, name, sizeof(Name));
    }

    size_t SizeOf() const {
        return offsetof(TMirrorHostLogel, Name) + strlen(Name) + 1;
    }
};

struct TMirrorGroupLogel {
    ui32 GroupId;
    ui32 NHosts;
    static const ui32 RecordSig = 0x12341407;

    TMirrorGroupLogel(ui32 groupId, ui32 nHosts)
        : GroupId(groupId)
        , NHosts(nHosts)
        {}

    size_t SizeOf() const { return sizeof(*this); }
};

#if defined (_MSC_VER)
#   pragma warning(pop)
#endif

#pragma pack(pop)
