#include <library/cpp/testing/gtest/gtest.h>

#include <yt/yt/core/misc/format.h>
#include <yt/yt/core/misc/ref.h>

namespace NYT {
namespace {

////////////////////////////////////////////////////////////////////////////////

static_assert(!TFormatTraits<TIntrusivePtr<TRefCounted>>::HasCustomFormatValue);

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT
