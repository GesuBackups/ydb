#include "random_generator.h"

#include <logbroker/unified_agent/common/util/clock.h>

#include <util/datetime/base.h>
#include <util/random/entropy.h>
#include <util/random/random.h>
#include <util/stream/str.h>

namespace NUnifiedAgent {
    TRandomGenerator::TRandomGenerator(size_t randomBufferSize)
        : RandomBuffer([&]() {
            auto result = TBuffer();
            result.Resize(randomBufferSize);
            Y_VERIFY(EntropyPool().Load(result.Data(), result.Size()) == result.Size());
            for (size_t i = 0; i < result.size(); ++i) {
                const auto c = static_cast<ui32>(result.Data()[i]);
                result.Data()[i] = static_cast<char>('a' + (c % ('z' - 'a' + 1)));
            }
            return result;
        }())
    {
    }

    void TRandomGenerator::GenerateRandomText(TString& target, size_t minSize, size_t maxSize) const {
        Y_VERIFY(minSize <= maxSize);
        const auto textSize = minSize + RandomNumber(maxSize - minSize + 1);
        const auto targetSize = target.Size();
        target.ReserveAndResize(targetSize + textSize);
        auto* p = target.begin() + targetSize;
        auto randomBufferIndex = RandomNumber<size_t>(RandomBuffer.size());
        auto bytesToCopy = textSize;
        while (bytesToCopy > 0) {
            const auto size = Min(RandomBuffer.size() - randomBufferIndex, bytesToCopy);
            memcpy(p, RandomBuffer.Data() + randomBufferIndex, size);
            p += size;
            randomBufferIndex += size;
            if (randomBufferIndex == RandomBuffer.size()) {
                randomBufferIndex = 0;
            }
            bytesToCopy -= size;
        }
    }

    TString TRandomGenerator::GenerateRandomText(size_t minSize, size_t maxSize) const {
        TString result;
        GenerateRandomText(result, minSize, maxSize);
        return result;
    }

    TSessionRandomGenerator::TSessionRandomGenerator(const TString& sessionKey,
        size_t minMessageSize,
        size_t maxMessageSize)
        : SessionKey(sessionKey)
        , MinMessageSize(minMessageSize)
        , MaxMessageSize(maxMessageSize)
        , NextId(0)
        , RandomGenerator()
    {
    }

    TString TSessionRandomGenerator::GenerateNewMessage() {
        TString result;
        {
            TStringOutput output(result);
            output << TClock::Now() << '|'
               << SessionKey << '|'
               << NextId.fetch_add(1) << '|';
        }

        const auto headersSize = result.Size();
        if (headersSize >= MaxMessageSize) {
            return result;
        }
        const auto maxPaddingSize = MaxMessageSize - headersSize;
        const auto minPaddingSize = headersSize < MinMessageSize
                                    ? MinMessageSize - headersSize
                                    : 0;
        RandomGenerator.GenerateRandomText(result, minPaddingSize, maxPaddingSize);
        return result;
    }
}
