#include "fls.h"
#include "atomic_flag_spinlock.h"

#include <library/cpp/ytalloc/api/ytalloc.h>

namespace NYT::NConcurrency::NDetail {

using namespace NYTAlloc;

////////////////////////////////////////////////////////////////////////////////

static const int FlsMaxSize = 256;
static std::atomic<int> FlsSize(0);

static std::atomic_flag FlsLock = ATOMIC_FLAG_INIT;
static TFlsSlotDtor FlsDestructors[FlsMaxSize];

// Thread-specific storage implementation.
// For native threads we use native TLS to store FLS.

int FlsAllocateSlot(TFlsSlotDtor dtor)
{
    // TODO: TForkAwareSpinLock
    TGuard<std::atomic_flag> guard(FlsLock);

    int index = FlsSize++;
    YT_VERIFY(index < FlsMaxSize);

    FlsDestructors[index] = dtor;

    return index;
}

int FlsCountSlots()
{
    return FlsSize;
}

uintptr_t FlsConstruct(TFlsSlotCtor ctor)
{
    TMemoryTagGuard guard(NullMemoryTag);
    return ctor();
}

void FlsDestruct(int index, uintptr_t value)
{
    FlsDestructors[index](value);
}

uintptr_t& TFsdHolder::FsdAt(int index)
{
    if (Y_UNLIKELY(index >= std::ssize(Fsd_))) {
        FsdResize();
    }
    return Fsd_[index];
}

void TFsdHolder::FsdResize()
{
    int oldSize = static_cast<int>(Fsd_.size());
    int newSize = FlsCountSlots();

    YT_ASSERT(newSize > oldSize);

    Fsd_.resize(newSize);

    for (int index = oldSize; index < newSize; ++index) {
        Fsd_[index] = 0;
    }
}

TFsdHolder::~TFsdHolder()
{
    for (int index = 0; index < std::ssize(Fsd_); ++index) {
        const auto& slot = Fsd_[index];
        if (slot) {
           FlsDestruct(index, slot);
        }
    }
}

static thread_local TFsdHolder TsdHolder;
thread_local TFsdHolder* CurrentFsdHolder = &TsdHolder;

TFsdHolder* SetCurrentFsdHolder(TFsdHolder* newFsd)
{
    auto currentFsd = CurrentFsdHolder;
    CurrentFsdHolder = newFsd ? newFsd : &TsdHolder;
    return currentFsd != &TsdHolder ? currentFsd : nullptr;
}

uintptr_t& FlsAt(int index)
{
    YT_ASSERT(CurrentFsdHolder);
    return CurrentFsdHolder->FsdAt(index);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NConcurrency::NDetail

