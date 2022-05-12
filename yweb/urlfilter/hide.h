#pragma once

#include <util/system/types.h>
#include <util/system/compiler.h>

template <class T, size_t Sz, class TAligner = size_t>
struct THiddenImpl {
    THiddenImpl() {
        static_assert(Sz >= sizeof(T), "expect Sz >= sizeof(T)");
        new (DataImpl) T;
    }
    ~THiddenImpl() {
        Obj().~T();
    }
    Y_FORCE_INLINE T& Obj() {
        return *reinterpret_cast<T*>(DataImpl);
    }
    Y_FORCE_INLINE const T& Obj() const {
        return *reinterpret_cast<const T*>(DataImpl);
    }

private:
    union {
        char DataImpl[Sz];
        TAligner A;
    };
};
