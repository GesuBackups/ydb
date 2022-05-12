#pragma once

#include <library/cpp/microbdb/microbdb.h>
#include "spiderrecords.h"

template <class val, typename pageiter>
class output_wormque_iterator: public TOutputRecordIterator<val, pageiter> {
    typedef TOutputRecordIterator<val, pageiter> reciter;

private:
    using pageiter::Freeze;
    using pageiter::Unfreeze;
    using reciter::Push;

public:
    const TUrlQuel *pushhost(const val *v) {
        fixhost();
        host = reciter::Push(v);
        pageiter::Freeze();
        return host;
    }

    const val *Push(const val *v, const typename TExtInfoType<val>::TResult *extInfo = NULL) {
        return reciter::Push(v, extInfo);
    }

    const TUrlQuel *pushurl(const val *v) {
        reciter::Push(v);
        if (reciter::Current())
            ++hostrecnum;
        return reciter::Current();
    }

protected:
    void fixhost() {
        if (host) {
            ((val *)host)->ModTime = hostrecnum + 1;
            host = NULL;
        }
        hostrecnum = 0;
        pageiter::Unfreeze();
    }

    int Init() {
        host = NULL;
        return reciter::Init();
    }

    int Term() {
        fixhost();
        return reciter::Term();
    }

    const TUrlQuel *host;
    int hostrecnum;
};

typedef TOutDatFileImpl<TUrlQuel,
    output_wormque_iterator<TUrlQuel, TOutputPageIterator<TOutputPageFile> >
> TOutWormQueue;
