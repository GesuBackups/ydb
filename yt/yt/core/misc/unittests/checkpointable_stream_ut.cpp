#include <yt/yt/core/test_framework/framework.h>

#include <yt/yt/core/misc/checkpointable_stream.h>

#include <util/stream/mem.h>
#include <util/stream/str.h>

#include <array>

namespace NYT {
namespace {

////////////////////////////////////////////////////////////////////////////////

TEST(TCheckpointableStreamTest, Simple)
{

    TStringStream stringOutput;
    auto output = CreateCheckpointableOutputStream(&stringOutput);

    output->Write("abc");
    output->Write("111");
    output->Write("ololo");

    TStringInput stringInput(stringOutput.Str());
    auto input = CreateCheckpointableInputStream(&stringInput);

    std::array<char, 10> buffer;

    EXPECT_EQ(0, input->GetOffset());
    EXPECT_EQ(2u, input->Read(buffer.data(), 2));
    EXPECT_EQ(2, input->GetOffset());
    EXPECT_EQ("ab", TStringBuf(buffer.data(), 2));

    EXPECT_EQ(2u, input->Read(buffer.data(), 2));
    EXPECT_EQ(4, input->GetOffset());
    EXPECT_EQ("c1", TStringBuf(buffer.data(), 2));

    EXPECT_EQ(7u, input->Read(buffer.data(), 10));
    EXPECT_EQ(11, input->GetOffset());
    EXPECT_EQ("11ololo", TStringBuf(buffer.data(), 7));
}

TEST(TCheckpointableStreamTest, Checkpoints)
{
    TStringStream stringOutput;
    auto output = CreateCheckpointableOutputStream(&stringOutput);

    output->Write("abc");
    output->Write("111");
    output->MakeCheckpoint();
    output->Write("u");
    output->MakeCheckpoint();
    output->Write("ololo");

    TStringInput stringInput(stringOutput.Str());
    auto input = CreateCheckpointableInputStream(&stringInput);

    std::array<char, 10> buffer;

    EXPECT_EQ(0, input->GetOffset());
    EXPECT_EQ(2u, input->Read(buffer.data(), 2));
    EXPECT_EQ(2, input->GetOffset());
    EXPECT_EQ("ab", TStringBuf(buffer.data(), 2));

    input->SkipToCheckpoint();

    EXPECT_EQ(6, input->GetOffset());
    EXPECT_EQ(1u, input->Read(buffer.data(), 1));
    EXPECT_EQ(7, input->GetOffset());
    EXPECT_EQ("u", TStringBuf(buffer.data(), 1));

    input->SkipToCheckpoint();

    EXPECT_EQ(7, input->GetOffset());
    EXPECT_EQ(2u, input->Read(buffer.data(), 2));
    EXPECT_EQ(9, input->GetOffset());
    EXPECT_EQ("ol", TStringBuf(buffer.data(), 2));

    EXPECT_EQ(2u, input->Read(buffer.data(), 2));
    EXPECT_EQ(11, input->GetOffset());
    EXPECT_EQ("ol", TStringBuf(buffer.data(), 2));

    input->SkipToCheckpoint();

    EXPECT_EQ(12, input->GetOffset());
    EXPECT_EQ(0u, input->Read(buffer.data(), 10));
}

TEST(TCheckpointableStreamTest, Buffered)
{
    TStringStream stringOutput;
    auto output = CreateCheckpointableOutputStream(&stringOutput);

    std::vector<char> blob;
    for (int i = 0; i < 1000; ++i) {
        blob.push_back(std::rand() % 128);
    }

    {
        auto bufferedOutput = CreateBufferedCheckpointableOutputStream(output.get(), 256);
        for (int i = 1; i <= 100; ++i) {
            for (int j = 0; j < i * 17; ++j) {
                bufferedOutput->Write((i + j) % 128);
            }
            bufferedOutput->Write(blob.data(), blob.size());
            bufferedOutput->MakeCheckpoint();
        }
    }

    TStringInput stringInput(stringOutput.Str());
    auto input = CreateCheckpointableInputStream(&stringInput);

    std::vector<char> buffer;
    for (int i = 1; i <= 100; ++i) {
        if (i % 2 == 0) {
            buffer.resize(i * 17);
            input->Load(buffer.data(), buffer.size());
            for (int j = 0; j < i * 17; ++j) {
                EXPECT_EQ((i + j) % 128, buffer[j]);
            }

            std::vector<char> blobCopy(blob.size());
            input->Load(blobCopy.data(), blobCopy.size());
            EXPECT_EQ(blob, blobCopy);
        }
        input->SkipToCheckpoint();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT
