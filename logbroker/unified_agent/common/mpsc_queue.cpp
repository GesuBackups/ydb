#include "mpsc_queue.h"

#include <util/system/compiler.h>

namespace NUnifiedAgent {
    TMpscQueue::TMpscQueue()
        : Head(&Stub)
        , Tail(&Stub)
    {
        Y_UNUSED(StubPadding);
        Y_UNUSED(HeadPadding);
        Y_UNUSED(TailPadding);
    }

    void TMpscQueue::Enqueue(TNode* node) noexcept {
        node->Next.store(nullptr, std::memory_order_release);
        auto prev = Head.exchange(node, std::memory_order_acq_rel);
        prev->Next.store(node, std::memory_order_release);
    }

    TMpscQueue::TNode* TMpscQueue::Dequeue() noexcept {
        auto tail = Tail;
        auto next = tail->Next.load(std::memory_order_acquire);

        // Handle stub node.
        if (tail == &Stub) {
            if (next == nullptr) {
                return nullptr;
            }
            Tail = next;
            // Save tail-recursive call by updating local variables.
            tail = next;
            next = next->Next.load(std::memory_order_acquire);
        }

        // No producer-consumer race.
        if (next) {
            Tail = next;
            return tail;
        }

        auto head = Head.load(std::memory_order_acquire);

        // Concurrent producer was blocked, bail out.
        if (tail != head) {
            return nullptr;
        }

        // Decouple (future) producers and consumer by barriering via stub node.
        Enqueue(&Stub);
        next = tail->Next.load(std::memory_order_acquire);

        if (next) {
            Tail = next;
            return tail;
        }

        return nullptr;
    }
}
