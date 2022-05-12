#pragma once

#include <type_traits>

namespace NUnifiedAgent {
    template <typename... Ts>
    struct TCheckValid {
        template <typename TF>
        static constexpr auto IsValid(TF) {
            return std::is_invocable_v<std::decay_t<TF>, Ts...>;
        }
    };

    #define IS_VALID_EXPANDER_END(...) (__VA_ARGS__){})

    #define IS_VALID(...) \
        TCheckValid<__VA_ARGS__>::IsValid([](auto _0) constexpr -> decltype IS_VALID_EXPANDER_END

    template<typename T>
    struct TArgumentType;

    template<typename T, typename U>
    struct TArgumentType<T(U)> {
        using TType = U;
    };
}
