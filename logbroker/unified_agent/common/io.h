#pragma once

#include <logbroker/unified_agent/common/tasks.h>

#include <util/network/socket.h>

namespace NUnifiedAgent {
    enum class EIODirection {
        Read,
        Write
    };

    class IIODispatcher {
    public:
        class TImpl;

        using TSubscriptionKey = std::pair<int, EIODirection>;

        class TSubscription {
        public:
            TSubscription(TImpl& owner, TSubscriptionKey key, TTaskHandle& target);

            ~TSubscription();

            void Renew();

            friend class TImpl;

        private:
            TImpl& Owner_;
            TSubscriptionKey Key_;
            TTaskHandle& Target_;
        };

    public:
        virtual ~IIODispatcher() = default;

        virtual void Start() = 0;

        virtual void Stop() = 0;

        virtual THolder<TSubscription> Subscribe(int fd, EIODirection direction, TTaskHandle& task) = 0;
    };

    THolder<IIODispatcher> MakeIODispatcher();

    class TNotificationHandle {
    public:
        explicit TNotificationHandle(bool nonBlock = true);

        ~TNotificationHandle();

        void Raise();

        bool Wait();

        inline SOCKET WaitSock() const noexcept {
            return Sockets[1];
        }

        inline SOCKET SignalSock() const noexcept {
            return Sockets[0];
        }

    private:
        SOCKET Sockets[2];
    };
}
