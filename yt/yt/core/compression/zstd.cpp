#include "zstd.h"
#include "details.h"
#include "private.h"

#include <yt/yt/core/misc/blob.h>
#include <yt/yt/core/misc/finally.h>

#define ZSTD_STATIC_LINKING_ONLY
#include <contrib/libs/zstd/include/zstd.h>

namespace NYT::NCompression {

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = CompressionLogger;

static constexpr size_t MaxZstdBlockSize = 1_MB;

////////////////////////////////////////////////////////////////////////////////

struct TZstdCompressBufferTag
{ };

////////////////////////////////////////////////////////////////////////////////

static size_t EstimateOutputSize(size_t totalInputSize)
{
    size_t headerSize = ZSTD_compressBound(0);
    size_t fullBlocksNumber = totalInputSize / MaxZstdBlockSize;
    size_t lastBlockSize = totalInputSize % MaxZstdBlockSize;
    return
        headerSize +
        fullBlocksNumber * ZSTD_compressBound(MaxZstdBlockSize) +
        ZSTD_compressBound(lastBlockSize);
}

void ZstdCompress(int level, StreamSource* source, TBlob* output)
{
    ui64 totalInputSize = source->Available();
    output->Resize(EstimateOutputSize(totalInputSize) + sizeof(totalInputSize), /* initializeStorage */ false);
    size_t curOutputPos = 0;

    // Write input size that will be used during decompression.
    {
        TMemoryOutput memoryOutput(output->Begin(), sizeof(totalInputSize));
        WritePod(memoryOutput, totalInputSize);
        curOutputPos += sizeof(totalInputSize);
    }

    auto context = ZSTD_createCCtx();
    auto contextGuard = Finally([&] () {
       ZSTD_freeCCtx(context);
    });

    // Write header.
    {
        size_t headerSize = ZSTD_compressBegin(context, level);

        YT_LOG_FATAL_IF(
            ZSTD_isError(headerSize),
            ZSTD_getErrorName(headerSize));

        curOutputPos += headerSize;
    }

    auto compressAndAppendBuffer = [&] (const void* buffer, size_t size) {
        if (size == 0) {
            return;
        }

        void* outputPtr = output->Begin() + curOutputPos;
        size_t compressedSize = ZSTD_compressContinue(
            context,
            outputPtr,
            output->Size() - curOutputPos,
            buffer,
            size);

        YT_LOG_FATAL_IF(
            ZSTD_isError(compressedSize),
            ZSTD_getErrorName(compressedSize));

        curOutputPos += compressedSize;
    };

    TBlob block(TZstdCompressBufferTag(), MaxZstdBlockSize, false);
    size_t blockSize = 0;
    auto flushBlock = [&] () {
        compressAndAppendBuffer(block.Begin(), blockSize);
        blockSize = 0;
    };

    while (source->Available()) {
        size_t inputSize;
        const void* inputPtr = source->Peek(&inputSize);
        size_t remainingSize = inputSize;

        auto fillBlock = [&] (size_t size) {
            memcpy(block.Begin() + blockSize, inputPtr, size);
            blockSize += size;
            source->Skip(size);
            inputPtr = source->Peek(&inputSize);
            remainingSize -= size;
        };

        if (blockSize != 0) {
            size_t takeSize = std::min(remainingSize, MaxZstdBlockSize - blockSize);
            fillBlock(takeSize);
            if (blockSize == MaxZstdBlockSize) {
                flushBlock();
            }
        }

        while (remainingSize >= MaxZstdBlockSize) {
            compressAndAppendBuffer(inputPtr, MaxZstdBlockSize);
            source->Skip(MaxZstdBlockSize);
            inputPtr = source->Peek(&inputSize);
            remainingSize -= MaxZstdBlockSize;
        }

        if (remainingSize > 0) {
            fillBlock(remainingSize);
        }

        YT_VERIFY(remainingSize == 0);
    }
    flushBlock();

    // Write footer.
    {
        void* outputPtr = output->Begin() + curOutputPos;
        size_t compressedSize = ZSTD_compressEnd(
            context,
            outputPtr,
            output->Size() - curOutputPos,
            nullptr,
            0);

        YT_LOG_FATAL_IF(
            ZSTD_isError(compressedSize),
            ZSTD_getErrorName(compressedSize));

        curOutputPos += compressedSize;
    }

    output->Resize(curOutputPos);
}

void ZstdDecompress(StreamSource* source, TBlob* output)
{
    ui64 outputSize;
    ReadPod(source, outputSize);
    output->Resize(outputSize, /* initializeStorage */ false);
    void* outputPtr = output->Begin();

    TBlob input;
    size_t inputSize;
    const void* inputPtr = source->Peek(&inputSize);

    if (source->Available() > inputSize) {
        Read(source, input);
        inputPtr = input.Begin();
        inputSize = input.Size();
    }

    size_t decompressedSize = ZSTD_decompress(outputPtr, outputSize, inputPtr, inputSize);

    // ZSTD_decompress returns error code instead of decompressed size if it fails.
    YT_LOG_FATAL_IF(
        ZSTD_isError(decompressedSize),
        ZSTD_getErrorName(decompressedSize));

    YT_VERIFY(decompressedSize == outputSize);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCompression::NYT

