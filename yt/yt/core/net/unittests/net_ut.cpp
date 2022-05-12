#include <yt/yt/core/test_framework/framework.h>

#include <yt/yt/core/net/connection.h>
#include <yt/yt/core/net/listener.h>
#include <yt/yt/core/net/dialer.h>
#include <yt/yt/core/net/config.h>
#include <yt/yt/core/net/private.h>

#include <yt/yt/core/concurrency/poller.h>
#include <yt/yt/core/concurrency/thread_pool_poller.h>

namespace NYT::NNet {
namespace {

using namespace NYT::NConcurrency;

////////////////////////////////////////////////////////////////////////////////

class TNetTest
    : public ::testing::Test
{
protected:
    IPollerPtr Poller;

    void SetUp() override
    {
        Poller = CreateThreadPoolPoller(2, "nettest");
    }

    void TearDown() override
    {
        Poller->Shutdown();
    }

    IDialerPtr CreateDialer()
    {
        return NNet::CreateDialer(
            New<TDialerConfig>(),
            Poller,
            NetLogger);
    }
};

TEST_F(TNetTest, ReconfigurePoller)
{
    for (int i = 1; i <= 10; ++i) {
        Poller->Reconfigure(i);
    }
    for (int i = 9; i >= 10; --i) {
        Poller->Reconfigure(i);
    }
}

TEST_F(TNetTest, CreateConnectionPair)
{
    IConnectionPtr a, b;
    std::tie(a, b) = CreateConnectionPair(Poller);
}

TEST_F(TNetTest, TransferFourBytes)
{
    IConnectionPtr a, b;
    std::tie(a, b) = CreateConnectionPair(Poller);

    a->Write(TSharedRef::FromString("ping")).Get();

    auto buffer = TSharedMutableRef::Allocate(10);
    ASSERT_EQ(4u, b->Read(buffer).Get().ValueOrThrow());
    ASSERT_EQ(ToString(buffer.Slice(0, 4)), TString("ping"));
}

TEST_F(TNetTest, TransferFourBytesUsingWriteV)
{
    IConnectionPtr a, b;
    std::tie(a, b) = CreateConnectionPair(Poller);

    a->WriteV(TSharedRefArray(std::vector<TSharedRef>{
        TSharedRef::FromString("p"),
        TSharedRef::FromString("i"),
        TSharedRef::FromString("n"),
        TSharedRef::FromString("g")
    }, TSharedRefArray::TMoveParts{})).Get().ThrowOnError();

    auto buffer = TSharedMutableRef::Allocate(10);
    ASSERT_EQ(4u, b->Read(buffer).Get().ValueOrThrow());
    ASSERT_EQ(ToString(buffer.Slice(0, 4)), TString("ping"));
}

TEST_F(TNetTest, BigTransfer)
{
    const int N = 1024, K = 256 * 1024;

    IConnectionPtr a, b;
    std::tie(a, b) = CreateConnectionPair(Poller);

    auto sender = BIND([=] {
        auto buffer = TSharedRef::FromString(TString(K, 'f'));
        for (int i = 0; i < N; ++i) {
            WaitFor(a->Write(buffer)).ThrowOnError();
        }

        WaitFor(a->CloseWrite()).ThrowOnError();
    })
        .AsyncVia(Poller->GetInvoker())
        .Run();

    auto receiver = BIND([=] {
        auto buffer = TSharedMutableRef::Allocate(3 * K);
        ssize_t received = 0;
        while (true) {
            int res = WaitFor(b->Read(buffer)).ValueOrThrow();
            if (res == 0) break;
            received += res;
        }

        EXPECT_EQ(N * K, received);
    })
        .AsyncVia(Poller->GetInvoker())
        .Run();

    sender.Get().ThrowOnError();
    receiver.Get().ThrowOnError();
}

TEST_F(TNetTest, BidirectionalTransfer)
{
    const int N = 1024, K = 256 * 1024;

    IConnectionPtr a, b;
    std::tie(a, b) = CreateConnectionPair(Poller);

    auto startSender = [&] (IConnectionPtr conn) {
        return BIND([=] {
            auto buffer = TSharedRef::FromString(TString(K, 'f'));
            for (int i = 0; i < N; ++i) {
                WaitFor(conn->Write(buffer)).ThrowOnError();
            }

            WaitFor(conn->CloseWrite()).ThrowOnError();
        })
            .AsyncVia(Poller->GetInvoker())
            .Run();
    };

    auto startReceiver = [&] (IConnectionPtr conn) {
        return BIND([=] {
            auto buffer = TSharedMutableRef::Allocate(K * 4);
            ssize_t received = 0;
            while (true) {
                int res = WaitFor(conn->Read(buffer)).ValueOrThrow();
                if (res == 0) break;
                received += res;
            }

            EXPECT_EQ(N * K, received);
        })
            .AsyncVia(Poller->GetInvoker())
            .Run();
    };

    std::vector<TFuture<void>> futures = {
        startSender(a),
        startReceiver(a),
        startSender(b),
        startReceiver(b)
    };

    AllSucceeded(futures).Get().ThrowOnError();
}

TEST_F(TNetTest, StressConcurrentClose)
{
    for (int i = 0; i < 10; i++) {
        IConnectionPtr a, b;
        std::tie(a, b) = CreateConnectionPair(Poller);

        auto runSender = [&] (IConnectionPtr conn) {
            return BIND([=] {
                auto buffer = TSharedRef::FromString(TString(16 * 1024, 'f'));
                while (true) {
                    WaitFor(conn->Write(buffer)).ThrowOnError();
                }
            })
                .AsyncVia(Poller->GetInvoker())
                .Run();
        };

        auto runReceiver = [&] (IConnectionPtr conn) {
            return BIND([=] {
                auto buffer = TSharedMutableRef::Allocate(16 * 1024);
                while (true) {
                    WaitFor(conn->Read(buffer)).ThrowOnError();
                }
            })
                .AsyncVia(Poller->GetInvoker())
                .Run();
        };

        runSender(a);
        runReceiver(a);
        runSender(b);
        runReceiver(b);

        Sleep(TDuration::MilliSeconds(10));
        a->Close().Get().ThrowOnError();
    }
}

////////////////////////////////////////////////////////////////////////////////

TEST_F(TNetTest, Bind)
{
    BIND([&] {
        auto address = TNetworkAddress::CreateIPv6Loopback(0);
        auto listener = CreateListener(address, Poller, Poller);
        EXPECT_THROW(CreateListener(listener->GetAddress(), Poller, Poller), TErrorException);
    })
        .AsyncVia(Poller->GetInvoker())
        .Run()
        .Get()
        .ThrowOnError();
}

TEST_F(TNetTest, DialError)
{
    BIND([&] {
        auto address = TNetworkAddress::CreateIPv6Loopback(4000);
        auto dialer = CreateDialer();
        EXPECT_THROW(dialer->Dial(address).Get().ValueOrThrow(), TErrorException);
    })
        .AsyncVia(Poller->GetInvoker())
        .Run()
        .Get()
        .ThrowOnError();
}

TEST_F(TNetTest, DialSuccess)
{
    BIND([&] {
        auto address = TNetworkAddress::CreateIPv6Loopback(0);
        auto listener = CreateListener(address, Poller, Poller);
        auto dialer = CreateDialer();

        auto futureDial = dialer->Dial(listener->GetAddress());
        auto futureAccept = listener->Accept();

        WaitFor(futureDial).ValueOrThrow();
        WaitFor(futureAccept).ValueOrThrow();
    })
        .AsyncVia(Poller->GetInvoker())
        .Run()
        .Get()
        .ThrowOnError();
}

TEST_F(TNetTest, ManyDials)
{
    BIND([&] {
        auto address = TNetworkAddress::CreateIPv6Loopback(0);
        auto listener = CreateListener(address, Poller, Poller);
        auto dialer = CreateDialer();

        auto futureAccept1 = listener->Accept();
        auto futureAccept2 = listener->Accept();

        auto futureDial1 = dialer->Dial(listener->GetAddress());
        auto futureDial2 = dialer->Dial(listener->GetAddress());

        WaitFor(AllSucceeded(std::vector<TFuture<IConnectionPtr>>{futureDial1, futureDial2, futureAccept1, futureAccept2})).ValueOrThrow();
    })
        .AsyncVia(Poller->GetInvoker())
        .Run()
        .Get()
        .ThrowOnError();
}

TEST_F(TNetTest, AbandonDial)
{
    BIND([&] {
        auto address = TNetworkAddress::CreateIPv6Loopback(0);
        auto listener = CreateListener(address, Poller, Poller);
        auto dialer = CreateDialer();

        dialer->Dial(listener->GetAddress());
    })
        .AsyncVia(Poller->GetInvoker())
        .Run()
        .Get()
        .ThrowOnError();
}

TEST_F(TNetTest, AbandonAccept)
{
    BIND([&] {
        auto address = TNetworkAddress::CreateIPv6Loopback(0);
        auto listener = CreateListener(address, Poller, Poller);

        listener->Accept();
    })
        .AsyncVia(Poller->GetInvoker())
        .Run()
        .Get()
        .ThrowOnError();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT::NNet
