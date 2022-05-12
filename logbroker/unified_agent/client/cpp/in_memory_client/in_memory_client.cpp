#include "in_memory_client.h"

namespace NUnifiedAgent {
    TInMemoryClient::TInMemoryClient(const TClientParameters& parameters)
        : ActiveSessions_()
        , Counters_(parameters.Counters ? parameters.Counters : MakeIntrusive<TClientCounters>())
        , Lock_()
    {
    }

    TClientSessionPtr TInMemoryClient::CreateSession(const TSessionParameters& parameters) {
        return MakeIntrusive<TInMemoryClientSession>(this, parameters);
    }

    TVector<TIntrusivePtr<TInMemorySessionStore>> TInMemoryClient::ActiveSessions() {
        with_lock(Lock_) {
            return ActiveSessions_;
        }
    }

    void TInMemoryClient::RegisterSession(TIntrusivePtr<TInMemorySessionStore> store) {
        with_lock(Lock_) {
            ActiveSessions_.push_back(store);
        }
    }

    void TInMemoryClient::UnregisterSession(TIntrusivePtr<TInMemorySessionStore> store) {
        with_lock(Lock_) {
            const auto it = Find(ActiveSessions_, store);
            Y_VERIFY(it != ActiveSessions_.end());
            ActiveSessions_.erase(it);
        }
    }

    TInMemorySessionStore::TInMemorySessionStore()
        : Messages_()
        , Lock_()
    {
    }

    TVector<TClientMessage> TInMemorySessionStore::ExtractMessages() {
        TVector<TClientMessage> result;
        with_lock(Lock_) {
            result.swap(Messages_);
        }
        return result;
    }

    void TInMemorySessionStore::AddMessage(TClientMessage&& message) {
        with_lock(Lock_) {
            Messages_.emplace_back(message);
        }
    }

    TInMemoryClientSession::TInMemoryClientSession(const TIntrusivePtr<TInMemoryClient>& client, const TSessionParameters& parameters)
        : Client_(client)
        , Counters_(parameters.Counters ? parameters.Counters : Client_->Counters()->GetDefaultSessionCounters())
        , OriginalSessionId_(MakeFMaybe(parameters.SessionId))
        , SessionId_(OriginalSessionId_)
        , Store_(MakeIntrusive<TInMemorySessionStore>())
    {
        Client_->RegisterSession(Store_);
        ++Client_->Counters()->ActiveSessionsCount;
    }

    void TInMemoryClientSession::Send(TClientMessage&& message) {
        const auto messageSize = SizeOf(message);
        ++Counters_->ReceivedMessages;
        Counters_->ReceivedBytes += messageSize;
        if (messageSize > 1_MB) {
            ++Counters_->DroppedMessages;
            Counters_->DroppedBytes += messageSize;
            ++Counters_->ErrorsCount;
            return;
        }
        if (message.Meta.Defined() && !NPrivate::IsUtf8(*message.Meta)) {
            ++Counters_->DroppedMessages;
            Counters_->DroppedBytes += messageSize;
            ++Counters_->ErrorsCount;
            return;
        }
        if (!message.Timestamp.Defined()) {
            message.Timestamp = TInstant::Now();
        }
        Store_->AddMessage(message);
    }

    ::NThreading::TFuture<void> TInMemoryClientSession::CloseAsync(TInstant) {
        return ::NThreading::MakeFuture();
    }

    TInMemoryClientSession::~TInMemoryClientSession() {
        Client_->UnregisterSession(Store_);
        --Client_->Counters()->ActiveSessionsCount;
    }

    TIntrusivePtr<TInMemoryClient> MakeInMemoryClient(const TClientParameters& parameters) {
        return MakeIntrusive<TInMemoryClient>(parameters);
    }
}
