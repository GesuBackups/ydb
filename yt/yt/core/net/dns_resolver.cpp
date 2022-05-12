#include "dns_resolver.h"
#include "private.h"

#include <yt/yt/core/actions/future.h>
#include <yt/yt/core/actions/invoker.h>

#include <yt/yt/core/net/address.h>

#include <yt/yt/core/misc/proc.h>

#include <yt/yt/core/concurrency/notification_handle.h>
#include <yt/yt/core/concurrency/delayed_executor.h>
#include <yt/yt/core/concurrency/thread.h>
#include <yt/yt/core/concurrency/moody_camel_concurrent_queue.h>

#include <yt/yt/core/profiling/timing.h>

#include <contrib/libs/c-ares/include/ares.h>

#ifdef _linux_
#define YT_DNS_RESOLVER_USE_EPOLL
#endif

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#ifdef YT_DNS_RESOLVER_USE_EPOLL
#include <sys/epoll.h>
#else
#include <sys/select.h>
#endif

////////////////////////////////////////////////////////////////////////////////

void FormatValue(NYT::TStringBuilderBase* builder, struct hostent* hostent, TStringBuf /*spec*/)
{
    bool empty = true;

    auto appendIf = [&] (bool condition, auto function) {
        if (condition) {
            if (!empty) {
                builder->AppendString(", ");
            }
            function();
            empty = false;
        }
    };

    builder->AppendString("{");

    appendIf(hostent->h_name, [&] {
        builder->AppendString("Canonical: ");
        builder->AppendString(hostent->h_name);
    });

    appendIf(hostent->h_aliases, [&] {
        builder->AppendString("Aliases: {");
        for (int i = 0; hostent->h_aliases[i]; ++i) {
            if (i > 0) {
                builder->AppendString(", ");
            }
            builder->AppendString(hostent->h_aliases[i]);
        }
        builder->AppendString("}");
    });

    appendIf(hostent->h_addr_list, [&] {
        auto stringSize = 0;
        if (hostent->h_addrtype == AF_INET) {
            stringSize = INET_ADDRSTRLEN;
        }
        if (hostent->h_addrtype == AF_INET6) {
            stringSize = INET6_ADDRSTRLEN;
        }
        builder->AppendString("Addresses: {");
        for (int i = 0; hostent->h_addr_list[i]; ++i) {
            if (i > 0) {
                builder->AppendString(", ");
            }
            auto string = builder->Preallocate(stringSize);
            ares_inet_ntop(
                hostent->h_addrtype,
                hostent->h_addr_list[i],
                string,
                stringSize);
            builder->Advance(strlen(string));
        }
        builder->AppendString("}");
    });

    builder->AppendString("}");
}

////////////////////////////////////////////////////////////////////////////////

namespace NYT::NNet {

using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = NetLogger;

////////////////////////////////////////////////////////////////////////////////

class TDnsResolver::TImpl
{
public:
    TImpl(
        int retries,
        TDuration resolveTimeout,
        TDuration maxResolveTimeout,
        TDuration warningTimeout,
        std::optional<double> jitter)
        : Retries_(retries)
        , ResolveTimeout_(resolveTimeout)
        , MaxResolveTimeout_(maxResolveTimeout)
        , WarningTimeout_(warningTimeout)
        , Jitter_(jitter)
        , ResolverThread_(New<TResolverThread>(this))
    {
    #ifdef YT_DNS_RESOLVER_USE_EPOLL
        EpollFD_ = HandleEintr(epoll_create1, EPOLL_CLOEXEC);
        YT_VERIFY(EpollFD_ >= 0);
    #endif

        int wakeupFD = WakeupHandle_.GetFD();
        OnSocketCreated(wakeupFD, AF_UNSPEC, this);
        OnSocketStateChanged(this, wakeupFD, 1, 0);

        // Init library globals.
        // c-ares 1.10+ provides recursive behaviour of init/cleanup.
        YT_VERIFY(ares_library_init(ARES_LIB_INIT_ALL) == ARES_SUCCESS);

        memset(&Channel_, 0, sizeof(Channel_));
        memset(&Options_, 0, sizeof(Options_));

        // See https://c-ares.haxx.se/ares_init_options.html for full details.
        int mask = 0;
        Options_.flags |= ARES_FLAG_STAYOPEN;
        mask |= ARES_OPT_FLAGS;
        Options_.timeout = static_cast<int>(ResolveTimeout_.MilliSeconds());
        mask |= ARES_OPT_TIMEOUTMS;

        Options_.maxtimeout = static_cast<int>(MaxResolveTimeout_.MilliSeconds());
        mask |= ARES_OPT_MAXTIMEOUTMS;

        if (Jitter_) {
            Options_.jitter = llround(*Jitter_ * 1000.0);
            Options_.jitter_rand_seed = TGuid::Create().Parts32[0];
            mask |= ARES_OPT_JITTER;
        }

        Options_.tries = Retries_;
        mask |= ARES_OPT_TRIES;

        Options_.sock_state_cb = &TDnsResolver::TImpl::OnSocketStateChanged;
        Options_.sock_state_cb_data = this;
        mask |= ARES_OPT_SOCK_STATE_CB;

        // Disable lookups from /etc/hosts file. Otherwise, cares re-reads file on every dns query.
        // That causes issues with memory management, because c-areas uses fopen(), and fopen() calls mmap().
        Options_.lookups = const_cast<char*>("b");

        YT_VERIFY(ares_init_options(&Channel_, &Options_, mask) == ARES_SUCCESS);

        ares_set_socket_callback(Channel_, &TDnsResolver::TImpl::OnSocketCreated, this);
    }

    ~TImpl()
    {
        ResolverThread_->Stop();

        ares_destroy(Channel_);

        // Cleanup library globals.
        // c-ares 1.10+ provides recursive behaviour of init/cleanup.
        ares_library_cleanup();

    #ifdef YT_DNS_RESOLVER_USE_EPOLL
        YT_VERIFY(HandleEintr(close, EpollFD_) == 0);
    #endif
    }

    TFuture<TNetworkAddress> ResolveName(
        TString hostName,
        bool enableIPv4,
        bool enableIPv6)
    {
        auto promise = NewPromise<TNetworkAddress>();
        auto future = promise.ToFuture();

        auto requestId = TGuid::Create();
        auto timeoutCookie = TDelayedExecutor::Submit(
            BIND([promise, requestId] () mutable {
                YT_LOG_WARNING("Resolve timed out (RequestId: %v)",
                    requestId);
                promise.TrySet(TError(EErrorCode::ResolveTimedOut, "Resolve timed out"));
            }),
            MaxResolveTimeout_);

        YT_LOG_DEBUG("Resolving host name (RequestId: %v, Host: %v, EnableIPv4: %v, EnableIPv6: %v)",
            requestId,
            hostName,
            enableIPv4,
            enableIPv6);

        auto request = std::unique_ptr<TNameRequest>{new TNameRequest{
            this,
            requestId,
            std::move(promise),
            std::move(hostName),
            enableIPv4,
            enableIPv6,
            {},
            std::move(timeoutCookie)
        }};

        Queue_.enqueue(std::move(request));

        WakeupHandle_.Raise();

        if (!ResolverThread_->Start()) {
            std::unique_ptr<TNameRequest> request;
            while (Queue_.try_dequeue(request)) {
                request->Promise.Set(TError(NYT::EErrorCode::Canceled, "DNS resolver is stopped"));
            }
        }

        return future;
    }

private:
    const int Retries_;
    const TDuration ResolveTimeout_;
    const TDuration MaxResolveTimeout_;
    const TDuration WarningTimeout_;
    const std::optional<double> Jitter_;

    struct TNameRequest
    {
        TImpl* Owner;
        TGuid RequestId;
        TPromise<TNetworkAddress> Promise;
        TString HostName;
        bool EnableIPv4;
        bool EnableIPv6;
        NProfiling::TWallTimer Timer;
        TDelayedExecutorCookie TimeoutCookie;
    };

    moodycamel::ConcurrentQueue<std::unique_ptr<TNameRequest>> Queue_;

#ifdef YT_DNS_RESOLVER_USE_EPOLL
    int EpollFD_ = -1;
#endif
    TNotificationHandle WakeupHandle_;

    ares_channel Channel_;
    ares_options Options_;

    class TResolverThread
        : public TThread
    {
    public:
        explicit TResolverThread(TImpl* owner)
            : TThread("DnsResolver")
            , Owner_(owner)
        { }

    private:
        TImpl* const Owner_;

        void StopPrologue() override
        {
            Owner_->WakeupHandle_.Raise();
        }

        void ThreadMain() override
        {
            constexpr size_t MaxRequestsPerDrain = 100;

            auto drainQueue = [&] {
                for (size_t iteration = 0; iteration < MaxRequestsPerDrain; ++iteration) {
                    std::unique_ptr<TNameRequest> request;
                    if (!Owner_->Queue_.try_dequeue(request)) {
                        return true;
                    }

                    // Try to reduce number of lookups to save some time.
                    int family = AF_UNSPEC;
                    if (request->EnableIPv4 && !request->EnableIPv6) {
                        family = AF_INET;
                    }
                    if (request->EnableIPv6 && !request->EnableIPv4) {
                        family = AF_INET6;
                    }

                    ares_gethostbyname(
                        Owner_->Channel_,
                        request->HostName.c_str(),
                        family,
                        &OnNameResolution,
                        request.get());

                    // Releasing unique_ptr on a separate line,
                    // because argument evaluation order is not specified.
                    request.release();
                }
                return false;
            };

            while (!IsStopping()) {
                bool drain = false;
                constexpr int PollTimeoutMs = 1000;

            #ifdef YT_DNS_RESOLVER_USE_EPOLL
                constexpr size_t MaxEventsPerPoll = 10;
                struct epoll_event events[MaxEventsPerPoll];
                int count = HandleEintr(epoll_wait, Owner_->EpollFD_, events, MaxEventsPerPoll, PollTimeoutMs);
                YT_VERIFY(count >= 0);

                if (count == 0) {
                    ares_process_fd(Owner_->Channel_, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
                } else {
                    // According to c-ares implementation this loop would cost O(#dns-servers-total * #dns-servers-active).
                    // Hope that we are not creating too many connections!
                    for (int i = 0; i < count; ++i) {
                        int triggeredFD = events[i].data.fd;
                        if (triggeredFD == Owner_->WakeupHandle_.GetFD()) {
                            drain = true;
                        } else {
                            // If the error events were returned, process both EPOLLIN and EPOLLOUT.
                            int readFD = (events[i].events & (EPOLLIN | EPOLLERR | EPOLLHUP)) ? triggeredFD : ARES_SOCKET_BAD;
                            int writeFD = (events[i].events & (EPOLLOUT | EPOLLERR | EPOLLHUP)) ? triggeredFD : ARES_SOCKET_BAD;
                            ares_process_fd(Owner_->Channel_, readFD, writeFD);
                        }
                    }
                }
            #else
                fd_set readFDs, writeFDs;
                FD_ZERO(&readFDs);
                FD_ZERO(&writeFDs);

                int wakeupFD = Owner_->WakeupHandle_.GetFD();
                int nFDs = ares_fds(Owner_->Channel_, &readFDs, &writeFDs);
                nFDs = std::max(nFDs, 1 + wakeupFD);
                FD_SET(wakeupFD, &readFDs);

                YT_VERIFY(nFDs <= FD_SETSIZE); // This is inherent limitation by select().

                timeval timeout;
                timeout.tv_sec = PollTimeoutMs / 1000;
                timeout.tv_usec = (PollTimeoutMs % 1000) * 1000;

                int result = select(nFDs, &readFDs, &writeFDs, nullptr, &timeout);
                YT_VERIFY(result >= 0);

                ares_process(Owner_->Channel_, &readFDs, &writeFDs);

                if (FD_ISSET(wakeupFD, &readFDs)) {
                    drain = true;
                }
            #endif

                if (drain && drainQueue()) {
                    Owner_->WakeupHandle_.Clear();
                    while (!drainQueue());
                }
            }

            // Make sure that there are no more enqueued requests.
            // We have observed `IsStopping() == true` previously,
            // which implies that producers no longer pushing to the queue.
            while (!drainQueue());
            // Cancel out all pending requests.
            ares_cancel(Owner_->Channel_);
        }
    };

    using TResolverThreadPtr = TIntrusivePtr<TResolverThread>;

    const TResolverThreadPtr ResolverThread_;


    static int OnSocketCreated(ares_socket_t socket, int /*type*/, void* opaque)
    {
        int result = 0;
    #ifdef YT_DNS_RESOLVER_USE_EPOLL
        auto this_ = static_cast<TDnsResolver::TImpl*>(opaque);
        struct epoll_event event{0, {0}};
        event.data.fd = socket;
        result = epoll_ctl(this_->EpollFD_, EPOLL_CTL_ADD, socket, &event);
        if (result != 0) {
            YT_LOG_WARNING(TError::FromSystem(), "epoll_ctl() failed");
            result = -1;
        }
    #else
        Y_UNUSED(opaque);
        if (socket >= FD_SETSIZE) {
            YT_LOG_WARNING("File descriptor is out of valid range (FD: %v, Limit: %v)",
                socket,
                FD_SETSIZE);
            result = -1;
        }
    #endif
        return result;
    }

    static void OnSocketStateChanged(void* opaque, ares_socket_t socket, int readable, int writable)
    {
    #ifdef YT_DNS_RESOLVER_USE_EPOLL
        auto this_ = static_cast<TDnsResolver::TImpl*>(opaque);
        struct epoll_event event{0, {0}};
        event.data.fd = socket;
        int op = EPOLL_CTL_MOD;
        if (readable) {
            event.events |= EPOLLIN;
        }
        if (writable) {
            event.events |= EPOLLOUT;
        }
        if (!readable && !writable) {
            op = EPOLL_CTL_DEL;
        }
        YT_VERIFY(epoll_ctl(this_->EpollFD_, op, socket, &event) == 0);
    #else
        Y_UNUSED(opaque, readable, writable);
        YT_VERIFY(socket < FD_SETSIZE);
    #endif
    }

    static void OnNameResolution(void* opaque, int status, int timeouts, struct hostent* hostent)
    {
        // TODO(sandello): Protect against exceptions here?
        auto request = std::unique_ptr<TNameRequest>{static_cast<TNameRequest*>(opaque)};

        TDelayedExecutor::CancelAndClear(request->TimeoutCookie);

        auto elapsed = request->Timer.GetElapsedTime();
        if (elapsed > request->Owner->WarningTimeout_ || timeouts > 0) {
            YT_LOG_WARNING("Resolve took too long (RequestId: %v, HostName: %v, Duration: %v, Timeouts: %v)",
                request->RequestId,
                request->HostName,
                elapsed,
                timeouts);
        }

        if (status != ARES_SUCCESS) {
            YT_LOG_WARNING("Resolve failed (RequestId: %v, HostName: %v)",
                request->RequestId,
                request->HostName);
            request->Promise.TrySet(TError("DNS resolve failed for %Qv",
                request->HostName)
                << TError(ares_strerror(status)));
            return;
        }

        YT_VERIFY(hostent->h_addrtype == AF_INET || hostent->h_addrtype == AF_INET6);
        YT_VERIFY(hostent->h_addr_list && hostent->h_addr_list[0]);

        TNetworkAddress result(hostent->h_addrtype, hostent->h_addr, hostent->h_length);
        YT_LOG_DEBUG("Host name resolved (RequestId: %v, HostName: %v, Result: %v, Hostent: %v)",
            request->RequestId,
            request->HostName,
            result,
            hostent);

        request->Promise.TrySet(result);
    }
};

////////////////////////////////////////////////////////////////////////////////

TDnsResolver::TDnsResolver(
    int retries,
    TDuration resolveTimeout,
    TDuration maxResolveTimeout,
    TDuration warningTimeout,
    std::optional<double> jitter)
    : Impl_(std::make_unique<TImpl>(
        retries,
        resolveTimeout,
        maxResolveTimeout,
        warningTimeout,
        jitter))
{ }

TDnsResolver::~TDnsResolver() = default;

TFuture<TNetworkAddress> TDnsResolver::ResolveName(
    TString hostName,
    bool enableIPv4,
    bool enableIPv6)
{
    return Impl_->ResolveName(
        std::move(hostName),
        enableIPv4,
        enableIPv6);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNet

