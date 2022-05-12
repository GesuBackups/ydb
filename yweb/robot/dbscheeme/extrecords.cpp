#include <library/cpp/microbdb/microbdb.h>

#include "pbrecords.h"

int TPbUrlQuel::BySerialized(const TPbUrlQuel *a, const TPbUrlQuel *b) {
    return NMicroBDB::CompareExtInfo(a, b);
}

int TPbUrlQuel::ByUrl(const TPbUrlQuel *a, const TPbUrlQuel *b) {
    static TPbUrlQuel::TExtInfo ae;
    static TPbUrlQuel::TExtInfo be;
    NMicroBDB::GetExtInfo(a, &ae);
    NMicroBDB::GetExtInfo(b, &be);
    int cmp;
    cmp = strcmp(ae.url().data(), be.url().data());
    if (cmp)
        return cmp;
    cmp = stricmp(ae.clienttype().data(), be.clienttype().data());
    if (cmp)
        return cmp;
    return NMicroBDB::CompareExtInfo(a, b);
}
