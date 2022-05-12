#include "lz4io.h"

#include <util/generic/scope.h>
#include <util/generic/size_literals.h>
#include <util/string/builder.h>

#include <contrib/libs/lz4/lz4.h>
#include <contrib/libs/lz4/lz4hc.h>
#define LZ4F_STATIC_LINKING_ONLY
#include <contrib/libs/lz4/lz4frame.h>

#include <ydb/library/yql/public/udf/udf_allocator.h>
#include <ydb/library/yql/public/udf/udf_terminator.h>

namespace NYql::NUdf {

namespace NLz4 {

namespace {

constexpr ui32 MagicNumberSize  = 4U;
constexpr ui32 Lz4ioMagicNumber =  0x184D2204U;
constexpr ui32 LegacyMagicNumber = 0x184C2102U;

constexpr size_t LegacyBlockSize = 8_MB;

void WriteLE32 (void* p, ui32 value32)
{
    const auto dstPtr = static_cast<unsigned char*>(p);
    dstPtr[0] = (unsigned char)value32;
    dstPtr[1] = (unsigned char)(value32 >> 8U);
    dstPtr[2] = (unsigned char)(value32 >> 16U);
    dstPtr[3] = (unsigned char)(value32 >> 24U);
}

ui32 ReadLE32 (const void* s) {
    const auto srcPtr =static_cast<const unsigned char*>(s);
    ui32 value32 = srcPtr[0];
    value32 += (ui32)srcPtr[1] <<  8U;
    value32 += (ui32)srcPtr[2] << 16U;
    value32 += (ui32)srcPtr[3] << 24U;
    return value32;
}

constexpr size_t BufferSize = 64_KB;

struct TDResources {
    void*  srcBuffer;
    size_t srcBufferSize;
    void*  dstBuffer;
    size_t dstBufferSize;
    LZ4F_decompressionContext_t dCtx;
    void*  dictBuffer;
    size_t dictBufferSize;
};

TDResources CreateDResources() {
    TDResources ress;

    if (const auto  errorCode = LZ4F_createDecompressionContext(&ress.dCtx, LZ4F_VERSION); LZ4F_isError(errorCode))
        UdfTerminate((TStringBuilder() << "Error : can't create LZ4F context resource: " << LZ4F_getErrorName(errorCode)).data());

    ress.srcBufferSize = BufferSize;
    ress.srcBuffer = UdfAllocateWithSize(ress.srcBufferSize);
    ress.dstBufferSize = BufferSize;
    ress.dstBuffer = UdfAllocateWithSize(ress.dstBufferSize);

    ress.dictBuffer = nullptr;
    ress.dictBufferSize = 0ULL;

    return ress;
}

void FreeDResources(TDResources ress)
{
    UdfFreeWithSize(ress.srcBuffer, ress.srcBufferSize);
    UdfFreeWithSize(ress.dstBuffer, ress.dstBufferSize);

    if (const auto errorCode = LZ4F_freeDecompressionContext(ress.dCtx); LZ4F_isError(errorCode))
        UdfTerminate((TStringBuilder() << "Error : can't free LZ4F context resource: " << LZ4F_getErrorName(errorCode)).data());
}

}

void DecompressFrame(IInputStream* input, IOutputStream* output)
{
    const auto ress = CreateDResources();
    Y_DEFER { FreeDResources(ress); };

    LZ4F_errorCode_t nextToLoad;

    {   size_t inSize = MagicNumberSize;
        size_t outSize = 0ULL;
        WriteLE32(ress.srcBuffer, Lz4ioMagicNumber);
        nextToLoad = LZ4F_decompress_usingDict(ress.dCtx, ress.dstBuffer, &outSize, ress.srcBuffer, &inSize, ress.dictBuffer, ress.dictBufferSize, nullptr);
        if (LZ4F_isError(nextToLoad))
            UdfTerminate((TStringBuilder() << "Header error: " << LZ4F_getErrorName(nextToLoad)).data());
    }

    while (nextToLoad) {
        size_t readSize;
        size_t pos = 0;
        size_t decodedBytes = ress.dstBufferSize;

        if (nextToLoad > ress.srcBufferSize)
            nextToLoad = ress.srcBufferSize;
        readSize = input->Read(ress.srcBuffer, nextToLoad);
        if (!readSize)
            break;

        while ((pos < readSize) || (decodedBytes == ress.dstBufferSize)) {
            size_t remaining = readSize - pos;
            decodedBytes = ress.dstBufferSize;
            nextToLoad = LZ4F_decompress_usingDict(ress.dCtx, ress.dstBuffer, &decodedBytes, (char*)(ress.srcBuffer)+pos, &remaining, ress.dictBuffer, ress.dictBufferSize, nullptr);
            if (LZ4F_isError(nextToLoad))
                UdfTerminate((TStringBuilder() << "Decompression error: " << LZ4F_getErrorName(nextToLoad)).data());
            pos += remaining;

            if (decodedBytes)
                output->Write(ress.dstBuffer, decodedBytes);

            if (!nextToLoad)
                break;
        }
    }

    if (nextToLoad != 0)
        UdfTerminate("Unfinished stream.");
}

void DecompressLegacy(IInputStream* input, IOutputStream* output)
{
    const auto in_buff = UdfAllocateWithSize(LZ4_compressBound(LegacyBlockSize));
    const auto out_buff = UdfAllocateWithSize(LegacyBlockSize);

    while (true) {
        unsigned int blockSize;

        if (const auto sizeCheck = input->Read(in_buff, 4U)) {
            if (sizeCheck != 4U)
                UdfTerminate("Read error : cannot access block size.");
            } else
                break;

            blockSize = ReadLE32(in_buff);
            if (blockSize > LZ4_COMPRESSBOUND(LegacyBlockSize)) {
                UdfTerminate("Read error : block size out of bounds.");
        }

        if (const auto sizeCheck = input->Read(in_buff, blockSize); sizeCheck != blockSize)
            UdfTerminate("Read error : cannot access compressed block.");

        if (const auto decodeSize = LZ4_decompress_safe(static_cast<char*>(in_buff), static_cast<char*>(out_buff), (int)blockSize, LegacyBlockSize); decodeSize < 0)
            UdfTerminate("Decoding Failed ! Corrupted input detected !");
        else
            output->Write(out_buff, (size_t)decodeSize);
    }

    UdfFreeWithSize(in_buff, LZ4_compressBound(LegacyBlockSize));
    UdfFreeWithSize(out_buff, LegacyBlockSize);
}

EStreamType CheckMagic(const void* data) {
    switch (ReadLE32(data)) {
        case Lz4ioMagicNumber:
            return EStreamType::Frame;
        case LegacyMagicNumber:
            return EStreamType::Legacy;
        default:
            return EStreamType::Unknown;
    }
}

}

}
