#pragma once

#include <logbroker/unified_agent/client/cpp/helpers.h>
#include <logbroker/unified_agent/client/cpp/client.h>
#include <logbroker/unified_agent/common/util/f_maybe.h>
#include <logbroker/unified_agent/client/cpp/counters.h>

#include <util/generic/vector.h>
#include <util/generic/size_literals.h>
#include <util/generic/maybe.h>
#include <util/datetime/base.h>
#include <util/generic/ptr.h>
#include <util/system/types.h>

namespace NUnifiedAgent {
    class TInMemoryClientSession;
    class TInMemorySessionStore;

    class TInMemoryClient: public IClient {
    public:
        explicit TInMemoryClient(const TClientParameters& parameters);

        TClientSessionPtr CreateSession(const TSessionParameters& parameters) override;

        inline TIntrusivePtr<TClientCounters>& Counters() noexcept {
            return Counters_;
        }

        TVector<TIntrusivePtr<TInMemorySessionStore>> ActiveSessions();

        friend class TInMemoryClientSession;

    private:
        void RegisterSession(TIntrusivePtr<TInMemorySessionStore> store);

        void UnregisterSession(TIntrusivePtr<TInMemorySessionStore> store);

    private:
        TVector<TIntrusivePtr<TInMemorySessionStore>> ActiveSessions_;
        TIntrusivePtr<TClientCounters> Counters_;
        TAdaptiveLock Lock_;
    };

    class TInMemoryClientSession: public IClientSession {
    public:
        TInMemoryClientSession(const TIntrusivePtr<TInMemoryClient>& client, const TSessionParameters& parameters);

        ~TInMemoryClientSession();

        void Send(TClientMessage&& message) override;

        NThreading::TFuture<void> CloseAsync(TInstant deadline) override;

        inline TInMemoryClient& Client() noexcept {
            return *Client_;
        }

        inline TClientSessionCounters& Counters() noexcept {
            return *Counters_;
        }

        inline const TIntrusivePtr<TInMemorySessionStore>& Store() noexcept {
            return Store_;
        }

    private:
        TIntrusivePtr<TInMemoryClient> Client_;
        TIntrusivePtr<TClientSessionCounters> Counters_;
        TFMaybe<TString> OriginalSessionId_;
        TFMaybe<TString> SessionId_;
        TIntrusivePtr<TInMemorySessionStore> Store_;
    };

    class TInMemorySessionStore final: public TAtomicRefCount<TInMemorySessionStore> {
    public:
        TInMemorySessionStore();

        TVector<TClientMessage> ExtractMessages();

        void AddMessage(TClientMessage&& message);

        void AddMessage(const TClientMessage& message) {
            AddMessage(TClientMessage(message));
        }

    private:
        TVector<TClientMessage> Messages_;
        TAdaptiveLock Lock_;
    };

    TIntrusivePtr<TInMemoryClient> MakeInMemoryClient(const TClientParameters& parameters = TClientParameters("."));
}
