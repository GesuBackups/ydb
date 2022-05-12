#include "chunked_output_stream.h"

#include <util/system/sanitizers.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

struct TDefaultChunkedOutputStreamTag
{ };

TChunkedOutputStream::TChunkedOutputStream(
    TRefCountedTypeCookie tagCookie,
    size_t initialReserveSize,
    size_t maxReserveSize)
    : MaxReserveSize_(RoundUpToPage(maxReserveSize))
    , CurrentReserveSize_(RoundUpToPage(initialReserveSize))
    , CurrentChunk_(tagCookie, 0, true)
{
    YT_VERIFY(MaxReserveSize_ > 0);

    if (CurrentReserveSize_ > MaxReserveSize_) {
        CurrentReserveSize_ = MaxReserveSize_;
    }
}

TChunkedOutputStream::TChunkedOutputStream()
    : TChunkedOutputStream(TDefaultChunkedOutputStreamTag())
{ }

std::vector<TSharedRef> TChunkedOutputStream::Flush()
{
    FinishedChunks_.push_back(TSharedRef::FromBlob(std::move(CurrentChunk_)));

    YT_ASSERT(CurrentChunk_.IsEmpty());
    FinishedSize_ = 0;

    for (auto& chunk : FinishedChunks_) {
        NSan::CheckMemIsInitialized(chunk.Begin(), chunk.Size());
    }

    return std::move(FinishedChunks_);
}

size_t TChunkedOutputStream::GetSize() const
{
    return FinishedSize_ + CurrentChunk_.Size();
}

size_t TChunkedOutputStream::GetCapacity() const
{
    return FinishedSize_ + CurrentChunk_.Capacity();
}

void TChunkedOutputStream::DoWrite(const void* buffer, size_t length)
{
    if (!CurrentChunk_.Capacity()) {
        CurrentChunk_.Reserve(CurrentReserveSize_);
    }

    const auto spaceAvailable = std::min(length, CurrentChunk_.Capacity() - CurrentChunk_.Size());
    const auto spaceRequired = length - spaceAvailable;

    CurrentChunk_.Append(buffer, spaceAvailable);

    if (spaceRequired) {
        YT_ASSERT(CurrentChunk_.Size() == CurrentChunk_.Capacity());

        FinishedSize_ += CurrentChunk_.Size();
        FinishedChunks_.push_back(TSharedRef::FromBlob(std::move(CurrentChunk_)));

        YT_ASSERT(CurrentChunk_.IsEmpty());

        CurrentReserveSize_ = std::min(2 * CurrentReserveSize_, MaxReserveSize_);

        CurrentChunk_.Reserve(std::max(RoundUpToPage(spaceRequired), CurrentReserveSize_));
        CurrentChunk_.Append(static_cast<const char*>(buffer) + spaceAvailable, spaceRequired);
    }
}

char* TChunkedOutputStream::Preallocate(size_t size)
{
    size_t available = CurrentChunk_.Capacity() - CurrentChunk_.Size();
    if (available < size) {
        FinishedSize_ += CurrentChunk_.Size();
        FinishedChunks_.push_back(TSharedRef::FromBlob(std::move(CurrentChunk_)));

        CurrentReserveSize_ = std::min(2 * CurrentReserveSize_, MaxReserveSize_);

        CurrentChunk_.Reserve(std::max(RoundUpToPage(size), CurrentReserveSize_));
    }
    return CurrentChunk_.End();
}

void TChunkedOutputStream::Advance(size_t size)
{
    YT_ASSERT(CurrentChunk_.Size() + size <= CurrentChunk_.Capacity());
    CurrentChunk_.Resize(CurrentChunk_.Size() + size, false);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
