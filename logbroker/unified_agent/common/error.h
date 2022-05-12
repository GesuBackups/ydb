#pragma once

#include <logbroker/unified_agent/common/util/f_maybe.h>

namespace NUnifiedAgent {
    struct TVoidMarker {
    };

    struct TError {
        TString Message;
    };

    inline auto Void() {
        return TVoidMarker();
    }

    template <typename T>
    class TErrorOr {
    public:
        TFMaybe<T> Value;
        TFMaybe<TString> Error;

        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U, T>>>
        inline TErrorOr(U&& value = {})
            : TErrorOr(std::move(value), Nothing())
        {
        }

        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U, T>>>
        inline TErrorOr(const U& value = {})
            : TErrorOr(value, Nothing())
        {
        }

        inline TErrorOr(TError&& error)
            : TErrorOr(Nothing(), std::move(error.Message))
        {
        }

        inline bool Ok() const {
            return !Error.Defined();
        }

        inline void ThrowIfError() const {
            if (Error) {
                throw yexception() << *Error;
            }
        }

        inline void Throw() const {
            Y_VERIFY(Error);
            throw yexception() << *Error;
        }

    private:
        inline TErrorOr(TFMaybe<T>&& value, TFMaybe<TString>&& error)
            : Value(std::move(value))
            , Error(std::move(error))
        {
        }
    };

    using TErrorOrVoid = TErrorOr<TVoidMarker>;
}
