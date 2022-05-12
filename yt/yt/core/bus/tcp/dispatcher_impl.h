#pragma once

#include "private.h"
#include "dispatcher.h"
#include "config.h"

#include <yt/yt/library/profiling/producer.h>

#include <yt/yt/library/syncmap/map.h>

#include <yt/yt/core/net/address.h>

#include <yt/yt/core/misc/error.h>
#include <yt/yt/core/misc/mpsc_stack.h>

#include <library/cpp/yt/threading/rw_spin_lock.h>

#include <atomic>

namespace NYT::NBus {

////////////////////////////////////////////////////////////////////////////////

NNet::TNetworkAddress GetLocalBusAddress(int port);
bool IsLocalBusTransportEnabled();

////////////////////////////////////////////////////////////////////////////////

class TTcpDispatcher::TImpl
    : public NProfiling::ISensorProducer
{
public:
    static const TIntrusivePtr<TImpl>& Get();

    const TTcpDispatcherCountersPtr& GetCounters(const TString& networkName);

    NConcurrency::IPollerPtr GetAcceptorPoller();
    NConcurrency::IPollerPtr GetXferPoller();

    void Configure(const TTcpDispatcherConfigPtr& config);

    void RegisterConnection(TTcpConnectionPtr connection);

    void ValidateNetworkingNotDisabled(EMessageDirection messageDirection) const;

    void CollectSensors(NProfiling::ISensorWriter* writer) override;

private:
    friend class TTcpDispatcher;

    DECLARE_NEW_FRIEND();

    void StartPeriodicExecutors();
    void OnLivenessCheck();

    NConcurrency::IPollerPtr GetOrCreatePoller(
        NConcurrency::IPollerPtr* poller,
        bool isXfer,
        const TString& threadNamePrefix);

    void DisableNetworking();
    bool IsNetworkingDisabled();

    YT_DECLARE_SPIN_LOCK(NThreading::TReaderWriterSpinLock, PollerLock_);
    TTcpDispatcherConfigPtr Config_ = New<TTcpDispatcherConfig>();
    NConcurrency::IPollerPtr AcceptorPoller_;
    NConcurrency::IPollerPtr XferPoller_;

    TMpscStack<TWeakPtr<TTcpConnection>> ConnectionsToRegister_;
    std::vector<TWeakPtr<TTcpConnection>> ConnectionList_;
    int CurrentConnectionListIndex_ = 0;

    struct TNetworkStatistics
    {
        TTcpDispatcherCountersPtr Counters = New<TTcpDispatcherCounters>();
    };

    NConcurrency::TSyncMap<TString, TNetworkStatistics> NetworkStatistics_;

    YT_DECLARE_SPIN_LOCK(NThreading::TSpinLock, PeriodicExecutorsLock_);
    NConcurrency::TPeriodicExecutorPtr ProfilingExecutor_;
    NConcurrency::TPeriodicExecutorPtr LivenessCheckExecutor_;

    std::atomic<bool> NetworkingDisabled_ = false;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NBus
