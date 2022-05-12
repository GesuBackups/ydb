#pragma once

#include "dbtypes.h"

#pragma pack(push, 4)

#if defined (_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable:4706) /*assignment within conditional expression*/
#   pragma warning(disable:4267) /*conversion from 'size_t' to 'type', possible loss of data*/
#endif

struct TLinkClickedLogel {
    ui16        LastAccess;
    ui16        NTimes;
    redir_t     Names;

    static const ui32 RecordSig = 0x37865201;

    size_t SizeOf() const {
        return strchr(strchr(Names,'\0') + 1, '\0') + 1 - (char*)this;
    }
};

#if defined (_MSC_VER)
#   pragma warning(pop)
#endif

#pragma pack(pop)
