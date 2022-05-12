#include "short_string_pool.h"

#include <util/system/sys_alloc.h>

namespace NUnifiedAgent {
    TShortStringDynamicPool::TShortStringDynamicPool(TInternedString maxId)
        : MaxId(maxId)
        , LastId(0)
        , DataSize(0)
        , UsedBytes_(0)
        , IndexByString()
        , IndexById()
    {
    }

    TShortStringDynamicPool::~TShortStringDynamicPool() {
        Y_VERIFY(IndexByString.size() == 0);
    }

    TStringBuf TShortStringDynamicPool::operator[](TInternedString s) const noexcept {
        const auto it = IndexById.find(s);
        Y_VERIFY(it != IndexById.end());
        return (**it).AsStringBuf();
    }

    void TShortStringDynamicPool::UnRef(TInternedString s) {
        const auto it = IndexById.find(s);
        Y_VERIFY(it != IndexById.end(), "%u", s);
        auto* slot = *it;
        Y_VERIFY(slot->Refs > 0, "%u", s);
        UsedBytes_ -= slot->Size();
        if (--slot->Refs > 0) {
            return;
        }
        IndexById.erase(it);
        Y_VERIFY(IndexByString.erase(slot->AsStringBuf()) == 1);
        DataSize -= slot->DataSize();
        y_deallocate(static_cast<void*>(slot));
    }

    TInternedString TShortStringDynamicPool::Ref(TStringBuf s) {
        Y_ENSURE(s.Size() < 256,
                TStringBuilder() << "value [" << s << "] size [" << s.Size() << "] exceeds the limit 255");

        auto& slot = **IndexByString.lazy_emplace(s, [&](const auto& ctor) {
            auto* data = y_allocate(sizeof(TSlot) + s.Size() + 1);
            auto* slot = reinterpret_cast<TSlot*>(data);
            slot->Refs = 0;
            s.copy(slot->Payload() + 1, s.Size());
            *slot->Payload() = static_cast<unsigned char>(s.Size());
            Y_VERIFY(IndexById.size() < MaxId);
            do {
                if (LastId == MaxId) {
                    LastId = 0;
                }
                slot->Id = ++LastId;
            } while (!IndexById.insert(slot).second);

            DataSize += slot->DataSize();

            ctor(slot);
        });
        ++slot.Refs;
        UsedBytes_ += slot.Size();
        return slot.Id;
    }

    TInternedString TShortStringDynamicPool::Find(TStringBuf s) const {
        const auto it = IndexByString.find(s);
        return it == IndexByString.end() ? 0 : (**it).Id;
    }
}
