#pragma once

#include <util/system/defaults.h>

template <class T>
struct TAutoZero {
    T Value_;

    inline TAutoZero()
        : Value_()
    {
    }
    inline TAutoZero(const T& value)
        : Value_(value)
    {
    }

    inline T& Value() {
        return Value_;
    }

    inline const T& Value() const {
        return Value_;
    }

    inline operator T&() {
        return Value_;
    }
    inline operator const T&() const {
        return Value_;
    }

    inline T* operator&() {
        return &Value_;
    }
    inline const T* operator&() const {
        return &Value_;
    }
};


template <class T>
T Max(const TAutoZero<T>& t, const T& x) {
    return Max((const T&)t, x);
}
template <class T>
T Max(const T& x, const TAutoZero<T>& t) {
    return Max(x, (const T&)t);
}
template <class T>
T Min(const TAutoZero<T>& t, const T& x) {
    return Min((const T&)t, x);
}
template <class T>
T Min(const T& x, const TAutoZero<T>& t) {
    return Min(x, (const T&)t);
}
