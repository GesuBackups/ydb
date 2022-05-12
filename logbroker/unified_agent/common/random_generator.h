#pragma once

#include <util/generic/buffer.h>
#include <util/generic/string.h>
#include <util/generic/size_literals.h>
#include <util/stream/buffer.h>

#include <atomic>

namespace NUnifiedAgent {
    class TRandomGenerator {
    public:
        explicit TRandomGenerator(size_t randomBufferSize = 10_MB);

        void GenerateRandomText(TString& target, size_t minSize, size_t maxSize) const;

        TString GenerateRandomText(size_t minSize, size_t maxSize) const;

    private:
        const TBuffer RandomBuffer;
    };

    class TSessionRandomGenerator {
    public:
        TSessionRandomGenerator(const TString& sessionKey, size_t minMessageSize, size_t maxMessageSize);

        TString GenerateNewMessage();

    private:
        const TString SessionKey;
        const size_t MinMessageSize;
        const size_t MaxMessageSize;
        std::atomic<size_t> NextId;
        const TRandomGenerator RandomGenerator;
    };
}
