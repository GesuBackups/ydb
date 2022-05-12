#include <yt/yt/core/test_framework/framework.h>

#include <yt/yt/core/misc/error.h>

#include <yt/yt/core/yson/string.h>

#include <yt/yt/core/ytree/convert.h>

namespace NYT {
namespace {

using namespace NYson;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

TEST(TErrorTest, SerializationDepthLimit)
{
    constexpr int Depth = 1000;
    auto error = TError(TErrorCode(Depth), "error");
    for (int i = Depth - 1; i >= 0; --i) {
        error = TError(TErrorCode(i), "error") << std::move(error);
    }

    // Use intermediate conversion to test YSON parser depth limit simultaneously.
    auto errorYson = ConvertToYsonString(error);
    auto errorNode = ConvertTo<IMapNodePtr>(errorYson);

    for (int i = 0; i < ErrorSerializationDepthLimit - 1; ++i) {
        ASSERT_EQ(errorNode->GetChildOrThrow("code")->GetValue<i64>(), i);
        ASSERT_EQ(errorNode->GetChildOrThrow("message")->GetValue<TString>(), "error");
        ASSERT_FALSE(errorNode->GetChildOrThrow("attributes")->AsMap()->FindChild("original_error_depth"));
        auto innerErrors = errorNode->GetChildOrThrow("inner_errors")->AsList()->GetChildren();
        ASSERT_EQ(innerErrors.size(), 1u);
        errorNode = innerErrors[0]->AsMap();
    }
    auto innerErrors = errorNode->GetChildOrThrow("inner_errors")->AsList();
    const auto& children = innerErrors->GetChildren();
    ASSERT_EQ(std::ssize(children), Depth - ErrorSerializationDepthLimit + 1);
    for (int i = 0; i < std::ssize(children); ++i) {
        auto child = children[i]->AsMap();
        ASSERT_EQ(child->GetChildOrThrow("code")->GetValue<i64>(), i + ErrorSerializationDepthLimit);
        ASSERT_EQ(child->GetChildOrThrow("message")->GetValue<TString>(), "error");
        auto originalErrorDepth = child->GetChildOrThrow("attributes")->AsMap()->FindChild("original_error_depth");
        if (i > 0) {
            ASSERT_TRUE(originalErrorDepth);
            ASSERT_EQ(originalErrorDepth->GetValue<i64>(), i + ErrorSerializationDepthLimit);
        } else {
            ASSERT_FALSE(originalErrorDepth);
        }
    }
}

TEST(TErrorTest, ErrorSkeletonStubImplementation)
{
    TError error("foo");
    EXPECT_THROW(error.GetSkeleton(), std::exception);
}

TEST(TErrorTest, FormatCtor)
{
    EXPECT_EQ("Some error %v", TError("Some error %v").GetMessage());
    EXPECT_EQ("Some error hello", TError("Some error %v", "hello").GetMessage());
}

TEST(TErrorTest, TruncateSimple)
{
    auto error = TError("Some error")
        << TErrorAttribute("my_attr", "Attr value");
    auto truncatedError = error.Truncate();
    EXPECT_EQ(error.GetCode(), truncatedError.GetCode());
    EXPECT_EQ(error.GetMessage(), truncatedError.GetMessage());
    EXPECT_EQ(error.GetPid(), truncatedError.GetPid());
    EXPECT_EQ(error.GetTid(), truncatedError.GetTid());
    EXPECT_EQ(error.GetSpanId(), truncatedError.GetSpanId());
    EXPECT_EQ(error.GetDatetime(), truncatedError.GetDatetime());
    EXPECT_EQ(error.Attributes().Get<TString>("my_attr"), truncatedError.Attributes().Get<TString>("my_attr"));
}

TEST(TErrorTest, TruncateLarge)
{
    auto error = TError("Some long long error");
    error.MutableAttributes()->Set("my_attr", "Some long long attr");

    auto truncatedError = error.Truncate(/*maxInnerErrorCount*/ 2, /*stringLimit*/ 10);
    EXPECT_EQ(error.GetCode(), truncatedError.GetCode());
    EXPECT_EQ("Some long ...<message truncated>", truncatedError.GetMessage());
    EXPECT_EQ("...<attribute truncated>...", truncatedError.Attributes().Get<TString>("my_attr"));
}

TEST(TErrorTest, YTExceptionToError)
{
    try {
        throw TSimpleException("message");
    } catch (const std::exception& ex) {
        TError error(ex);
        EXPECT_EQ(NYT::EErrorCode::Generic, error.GetCode());
        EXPECT_EQ("message", error.GetMessage());
    }
}

TEST(TErrorTest, CompositeYTExceptionToError)
{
    try {
        try {
            throw TSimpleException("inner message");
        } catch (const std::exception& ex) {
            throw TCompositeException(ex, "outer message");
        }
    } catch (const std::exception& ex) {
        TError outerError(ex);
        EXPECT_EQ(NYT::EErrorCode::Generic, outerError.GetCode());
        EXPECT_EQ("outer message", outerError.GetMessage());
        EXPECT_EQ(1, std::ssize(outerError.InnerErrors()));
        const auto& innerError = outerError.InnerErrors()[0];
        EXPECT_EQ(NYT::EErrorCode::Generic, innerError.GetCode());
        EXPECT_EQ("inner message", innerError.GetMessage());
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT
