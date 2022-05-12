#include "io.h"

#include <logbroker/unified_agent/common/system.h>
#include <logbroker/unified_agent/common/util/f_maybe.h>

#include <library/cpp/containers/absl_flat_hash/flat_hash_map.h>

#include <util/generic/hash.h>
#include <util/network/pair.h>
#include <util/string/cast.h>
#include <util/system/thread.h>

#ifdef _linux_
#include <sys/epoll.h>
#include <sys/eventfd.h>
#else
#include <util/network/poller.h>
#endif

namespace NUnifiedAgent {
#ifdef _linux_
    namespace {
        int DoEpollCreate1(int flags) {
            const int result = ::epoll_create1(flags);
            Y_VERIFY(result != -1, "epoll_create1 failed, result [%d], errno [%d]", result, errno);
            return result;
        }

        int DoEpollCtl(int epfd, int op, int fd, struct epoll_event* event) {
            const int result = ::epoll_ctl(epfd, op, fd, event);
            Y_VERIFY(result != -1, "epoll_ctl failed, result [%d], errno [%d]", result, errno);
            return result;
        }

        int DoEpollWait(int epfd, struct epoll_event* evlist, int maxevents, int timeout) {
            while (true) {
                const int result = ::epoll_wait(epfd, evlist, maxevents, timeout);
                if (result == -1) {
                    if (errno == EINTR) {
                        continue;
                    }
                    Y_FAIL("epoll_wait failed, result [%d], errno [%d]", result, errno);
                }
                return result;
            }
        }

        class TListener {
        public:
            TListener()
                : EPollFd(DoEpollCreate1(0))
                , EpollEvents(50)
                , Count(0)
                , Index(0)
            {
            }

            ~TListener() {
                DoClose(EPollFd);
            }

            void Arm(IIODispatcher::TSubscriptionKey* key) {
                epoll_event ev{};
                ev.data.fd = key->first;
                ev.events = (key->second == EIODirection::Read ? EPOLLIN : EPOLLOUT) | EPOLLET;
                DoEpollCtl(EPollFd, EPOLL_CTL_ADD, key->first, &ev);
            }

            inline void Renew(IIODispatcher::TSubscriptionKey*) {
            }

            inline void Unarm(int fd) {
                DoEpollCtl(EPollFd, EPOLL_CTL_DEL, fd, nullptr);
            }

            IIODispatcher::TSubscriptionKey Listen() {
                while (true) {
                    if (Index == Count) {
                        Count = DoEpollWait(EPollFd, EpollEvents.data(), EpollEvents.size(), -1);
                        Y_VERIFY(Count > 0);
                        Index = 0;
                    }

                    auto& event = EpollEvents[Index];
                    if ((event.events & EPOLLIN) != 0) {
                        event.events &= ~EPOLLIN;
                        return {event.data.fd, EIODirection::Read};
                    }
                    if ((event.events & EPOLLOUT) != 0) {
                        event.events &= ~EPOLLOUT;
                        return {event.data.fd, EIODirection::Write};
                    }
                    ++Index;
                }
            }

        private:
            int EPollFd;
            TVector<epoll_event> EpollEvents;
            size_t Count;
            size_t Index;
        };
    }
#else
    namespace {
        class TListener {
        public:
            TListener()
                : Poller()
            {
            }

            inline void Arm(IIODispatcher::TSubscriptionKey* key) {
                if (key->second == EIODirection::Read) {
                    Poller.WaitReadOneShot(key->first, key);
                } else {
                    Poller.WaitWriteOneShot(key->first, key);
                }
            }

            inline void Renew(IIODispatcher::TSubscriptionKey* key) {
                Arm(key);
            }

            inline void Unarm(int fd) {
                Poller.Unwait(fd);
            }

            inline IIODispatcher::TSubscriptionKey Listen() {
                return *static_cast<IIODispatcher::TSubscriptionKey*>(Poller.WaitI());
            }

        private:
            TSocketPoller Poller;
        };
    }

#endif
    class IIODispatcher::TImpl: public IIODispatcher {
    public:
        explicit TImpl()
            : Listener()
            , PollThread()
            , Subscriptions()
            , Started(false)
            , ShutdownHandle()
            , ShutdownKey({ShutdownHandle.WaitSock(), EIODirection::Read})
            , Lock()
        {
            Listener.Arm(&ShutdownKey);
        }

        ~TImpl() override {
            try {
                Stop();
            } catch(...) {
                Y_FAIL("stop error: %s", CurrentExceptionMessage().c_str());
            }
        }

        void Start() override {
            with_lock(Lock) {
                Y_VERIFY(!Started);
                Y_VERIFY(!PollThread.joinable());
                Started = true;
            }

            PollThread = std::thread(&IIODispatcher::TImpl::Poll, this);
        }

        void Stop() override {
            with_lock(Lock) {
                Y_VERIFY(Subscriptions.empty(), "there must be no active subscriptions");
                if (!Started) {
                    return;
                }
                Started = false;
                Y_VERIFY(PollThread.joinable());
            }

            ShutdownHandle.Raise();

            PollThread.join();
        }

        THolder<TSubscription> Subscribe(int fd, EIODirection direction, TTaskHandle& task) override {
            const auto key = TSubscriptionKey{fd, direction};
            auto subscription = MakeHolder<TSubscription>(*this, key, task);
            with_lock(Lock) {
                Y_VERIFY(Subscriptions.insert({key, subscription.Get()}).second,
                    "fd [%d] already subscribed on [%s]",
                    fd, ToString(direction).c_str());
            }
            Listener.Arm(&subscription->Key_);
            return subscription;
        }

        void Unsubscribe(TSubscriptionKey* key) {
            with_lock(Lock) {
                auto it = Subscriptions.find(*key);
                Y_VERIFY(it != Subscriptions.end(), "fd [%d] is not subscribed for [%s]",
                    key->first, ToString(key->second).c_str());
                Subscriptions.erase(it);
            }
            Listener.Unarm(key->first);
        }

        inline void Renew(TSubscriptionKey* key) {
            Listener.Renew(key);
        }

    private:
        void Poll() noexcept {
            TThread::SetCurrentThreadName("ua_io_disp");

            while (true) {
                const auto key = Listener.Listen();
                if (key.first == ShutdownHandle.WaitSock()) {
                    break;
                }
                with_lock(Lock) {
                    const auto it = Subscriptions.find(key);
                    if (it != Subscriptions.end()) {
                        it->second->Target_.Pulse();
                    }
                }
            }
        }

    private:
        TListener Listener;
        std::thread PollThread;
        absl::flat_hash_map<TSubscriptionKey, TSubscription*> Subscriptions;
        bool Started;
        TNotificationHandle ShutdownHandle;
        TSubscriptionKey ShutdownKey;
        TAdaptiveLock Lock;
    };

    TNotificationHandle::TNotificationHandle(bool nonBlock)
        : Sockets()
    {
        {
            const int result = SocketPair(Sockets);
            Y_VERIFY(result == 0, "SocketPair failed, errno [%d], result [%d]", errno, result);
        }

        if (nonBlock) {
            SetNonBlock(Sockets[0]);
            SetNonBlock(Sockets[1]);
        }
    }

    TNotificationHandle::~TNotificationHandle() {
        closesocket(Sockets[0]);
        closesocket(Sockets[1]);
    }

    void TNotificationHandle::Raise() {
        char ch = 13;

        ::send(SignalSock(), &ch, 1, 0);
    }

    bool TNotificationHandle::Wait() {
        char ch;
        return ::recv(WaitSock(), &ch, 1, 0) == 1;
    }

    IIODispatcher::TSubscription::TSubscription(TImpl& owner, TSubscriptionKey key, TTaskHandle& target)
        : Owner_(owner)
        , Key_(key)
        , Target_(target)
    {
    }

    IIODispatcher::TSubscription::~TSubscription() {
        Owner_.Unsubscribe(&Key_);
    }

    void IIODispatcher::TSubscription::Renew() {
        Owner_.Renew(&Key_);
    }

    THolder<IIODispatcher> MakeIODispatcher() {
        return MakeHolder<IIODispatcher::TImpl>();
    }
}
