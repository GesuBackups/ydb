#pragma once

#include <library/cpp/containers/absl_flat_hash/flat_hash_set.h>
#include <logbroker/unified_agent/common/util/f_maybe.h>

#include <util/generic/buffer.h>
#include <util/generic/strbuf.h>
#include <util/generic/vector.h>
#include <util/generic/deque.h>
#include <util/str_stl.h>
#include <util/string/builder.h>

namespace NUnifiedAgent {
    using TInternedString = ui32;

    template <typename T>
    inline size_t IndirectSizeOf(const T& container) noexcept {
        return sizeof(typename T::slot_type) * container.capacity();
    }

    class IStringResolver {
    public:
        virtual ~IStringResolver() = default;

        virtual TStringBuf Resolve(TInternedString s) const = 0;

        virtual TInternedString Find(TStringBuf s) const = 0;

        inline TInternedString Get(TStringBuf s) const {
            const auto result = Find(s);
            Y_VERIFY(result != 0, "can't find label [%s]", TString(s).c_str());
            return result;
        }
    };

    class TShortStringStaticPool: public TNonCopyable, public IStringResolver {
    public:
        static constexpr size_t MaxLabelSize = 255;

    public:
        TShortStringStaticPool()
            : Data()
            , Index(0, THashFunc{this}, TEqFunc{this})
        {
        }

        TShortStringStaticPool(const TShortStringStaticPool& other)
            : Data(other.Data)
            , Index(begin(other.Index), end(other.Index), other.Index.bucket_count(), THashFunc{this}, TEqFunc{this})
        {
        }

        TInternedString Find(TStringBuf s) const override {
            const auto it = Index.find(s);
            return it == Index.end() ? 0 : *it;
        }

        TStringBuf operator[](TInternedString s) const noexcept {
            const char* p = Data.Data() + s;
            return TStringBuf(p, static_cast<unsigned char>(p[-1]));
        }

        TInternedString operator[](TStringBuf s) {
            Y_ENSURE(s.Size() <= MaxLabelSize,
                TStringBuilder() << "value [" << s << "] size [" << s.Size() << "] exceeds the limit " << MaxLabelSize);

            return *Index.lazy_emplace(s, [&](const auto& ctor) {
                Data.Append(static_cast<unsigned char>(s.Size()));
                const auto index = static_cast<ui32>(Data.size());
                Data.Append(s.Data(), s.Size());
                ctor(index);
            });
        }

        TStringBuf Resolve(TInternedString s) const override {
            return (*this)[s];
        }

        inline size_t IndirectSize() const noexcept {
            return Data.Capacity() + IndirectSizeOf(Index);
        }

    private:
        struct THashFunc {
            using is_transparent = void;

            inline size_t operator()(TInternedString k) const noexcept {
                return THash<TStringBuf>()((*Pool)[k]);
            }

            inline size_t operator()(TStringBuf k) const noexcept {
                return THash<TStringBuf>()(k);
            }

            TShortStringStaticPool* Pool;
        };

        struct TEqFunc {
            using is_transparent = void;

            inline size_t operator()(TInternedString a, TInternedString b) const noexcept {
                return a == b;
            }

            inline size_t operator()(TInternedString a, TStringBuf k) const noexcept {
                return (*Pool)[a] == k;
            }

            TShortStringStaticPool* Pool;
        };

    private:
        TBuffer Data;
        absl::flat_hash_set<TInternedString, THashFunc, TEqFunc> Index;
    };

    class TShortStringDynamicPool: public IStringResolver {
    public:
        explicit TShortStringDynamicPool(TInternedString maxId = std::numeric_limits<TInternedString>::max());

        ~TShortStringDynamicPool();

        TInternedString Ref(TStringBuf s);

        TStringBuf operator[](TInternedString s) const noexcept;

        TStringBuf Resolve(TInternedString s) const override {
            return (*this)[s];
        }

        TInternedString Find(TStringBuf s) const override;

        inline size_t Count() const noexcept {
            return IndexByString.size();
        }

        void UnRef(TInternedString s);

        inline size_t FootprintBytes() const noexcept {
            return DataSize + IndirectSizeOf(IndexByString) + IndirectSizeOf(IndexById);
        }

        inline size_t UsedBytes() const noexcept {
            return UsedBytes_;
        }

    private:
        struct TSlot {
            ui32 Refs;
            ui32 Id;

            inline char* Payload() noexcept {
                return reinterpret_cast<char*>(this + 1);
            }

            inline const char* Payload() const noexcept {
                return reinterpret_cast<const char*>(this + 1);
            }

            inline ui8 Size() const noexcept {
                return static_cast<unsigned char>(*Payload());
            }

            inline TStringBuf AsStringBuf() const noexcept {
                return TStringBuf(Payload() + 1, Size());
            }

            inline size_t DataSize() const noexcept {
                return sizeof(TSlot) + Size() + 1;
            }
        };

        struct THashByStrFunc {
            using is_transparent = void;

            inline size_t operator()(TSlot* a) const noexcept {
                return THash<TStringBuf>()(a->AsStringBuf());
            }

            inline size_t operator()(const TStringBuf& s) const noexcept {
                return THash<TStringBuf>()(s);
            }
        };

        struct TEqByStrFunc {
            using is_transparent = void;

            inline bool operator()(const TSlot* a, const TSlot* b) const noexcept;

            inline size_t operator()(const TSlot* a, const TStringBuf& b) const noexcept {
                return a->AsStringBuf() == b;
            }
        };

        struct THashByIdFunc {
            using is_transparent = void;

            inline size_t operator()(const TSlot* a) const noexcept {
                return NumericHash(a->Id);
            }

            inline size_t operator()(TInternedString a) const noexcept {
                return NumericHash(a);
            }
        };

        struct TEqByIdFunc {
            using is_transparent = void;

            inline bool operator()(const TSlot* a, const TSlot* b) const noexcept {
                return a->Id == b->Id;
            }

            inline size_t operator()(const TSlot* a, TInternedString b) const noexcept {
                return a->Id == b;
            }
        };

    private:
        const TInternedString MaxId;
        TInternedString LastId;
        size_t DataSize;
        size_t UsedBytes_;

        // overhead: 8 + 8 + 4 + 4 + 1 + 4 = 29 bytes per string
        // it is less than for TString, for witch it is 32 bytes, but seems too much nevertheless (
        // todo: devise a more adequate scheme
        absl::flat_hash_set<TSlot*, THashByStrFunc, TEqByStrFunc> IndexByString;
        absl::flat_hash_set<TSlot*, THashByIdFunc, TEqByIdFunc> IndexById;
    };
}
