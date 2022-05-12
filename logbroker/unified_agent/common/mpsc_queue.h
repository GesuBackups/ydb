#pragma once

#include <atomic>

namespace NUnifiedAgent {
    //shamelessly stolen from yt/19_4/yt/core/misc/mpsc_queue.h
    class TMpscQueue {
    public:
        struct TNode {
            std::atomic<TNode*> Next = {nullptr};
        };

    public:
        TMpscQueue();

        void Enqueue(TNode* node) noexcept;

        TNode* Dequeue() noexcept;

    private:
        TNode Stub;
        char StubPadding[64 - sizeof(Stub)];
        std::atomic<TNode*> Head;
        char HeadPadding[64 - sizeof(Head)];
        TNode* Tail;
        char TailPadding[64 - sizeof(Tail)];
    };
}
