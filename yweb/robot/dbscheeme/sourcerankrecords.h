/// @author Victor Ploshikhin vvp@
/// created Feb 9, 2011 4:44:49 PM

#pragma once

#include <library/cpp/microbdb/sorterdef.h>

#include "dbtypes.h"

#pragma pack(push, 4)

typedef double TRankType;


/**
 * Use it in preparat data flow to deliver source rank to xmap generation.
 */
struct TSourceRankRec {
    ui32        HostId;
    ui64        UrlId;
    TRankType   Rank;

    static const ui32 RecordSig = 0x20110210;

    TSourceRankRec(const ui32 hostId = 0, const ui64 urlId = 0, const TRankType rank = 0)
        : HostId(hostId)
        , UrlId(urlId)
        , Rank(rank)
    {
    }

    static int ByHostUrlIdRank(const TSourceRankRec* r1, const TSourceRankRec* r2)
    {
        int res = CompareUids(r1, r2);
        if ( 0 == res ) {
            res = ::compare(r2->Rank, r1->Rank); // by rank
        }
        return res;
    }

    bool operator < (const TSourceRankRec& r) const
    {
        return (ByHostUrlIdRank(this, &r) < 0);
    }
};

MAKESORTERTMPL(TSourceRankRec, ByHostUrlIdRank);

#pragma pack(pop)
