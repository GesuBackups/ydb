#pragma once

#include <library/cpp/digest/crc32c/crc32c.h>

#include <type_traits>

namespace NUnifiedAgent {
    template <typename T>
    ui32 ComputeCrc(const T& value) {
        static_assert(offsetof(T, Crc) == 0, "crc must be the first field");

        return Crc32c(reinterpret_cast<const char*>(&value) + sizeof(value.Crc),
            sizeof(value) - sizeof(value.Crc));
    }

    struct TFileHeader {
        ui32 Crc;
        ui32 Magic;
        ui32 FormatVersion;
        ui32 Padding;

        static constexpr ui32 ValidMagic = 2109870321;
        static constexpr ui32 CurrentFormatVersion = 1;

        template <typename T>
        static void PrepareHeader(T& header);

        template <typename T>
        static bool CheckHeader(const T& header);
    };
    static_assert(sizeof(TFileHeader) == 16,
        "invalid size of TFileHeader, expected 16");

    template <typename T>
    void TFileHeader::PrepareHeader(T& header) {
        static_assert(std::is_base_of_v<TFileHeader, T>);

        header.Magic = ValidMagic;
        header.FormatVersion = CurrentFormatVersion;
        header.Padding = 0;
        header.Crc = ComputeCrc(header);
    }

    template <typename T>
    bool TFileHeader::CheckHeader(const T& header) {
        static_assert(std::is_base_of_v<TFileHeader, T>);

        return header.Magic == ValidMagic &&
               (header.FormatVersion == CurrentFormatVersion || header.FormatVersion == 0) &&
               header.Padding == 0 &&
               ComputeCrc(header) == header.Crc;
    }
}
