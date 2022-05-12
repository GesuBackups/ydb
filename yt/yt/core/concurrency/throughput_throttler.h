#pragma once

#include "public.h"

#include <yt/yt/core/actions/future.h>

#include <yt/yt/core/logging/log.h>

#include <yt/yt/core/ypath/public.h>

#include <yt/yt/core/ytree/yson_serializable.h>

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NConcurrency {

////////////////////////////////////////////////////////////////////////////////

//! Enables throttling sync and async operations.
/*!
 *  This interface and its implementations are vastly inspired by the "token bucket" algorithm and
 *  |DataTransferThrottler| class from Hadoop.
 *
 *  Thread affinity: any
 */
struct IThroughputThrottler
    : public virtual TRefCounted
{
    //! Assuming that we are about to transfer #count bytes,
    //! returns a future that is set when enough time has passed
    //! to ensure proper bandwidth utilization.
    /*!
     *  \note Thread affinity: any
     */
    virtual TFuture<void> Throttle(i64 count) = 0;

    //! Tries to acquire #count bytes for transfer.
    //! Returns |true| if the request could be served without overdraft.
    /*!
     *  \note Thread affinity: any
     */
    virtual bool TryAcquire(i64 count) = 0;

    //! Tries to acquire #count bytes for transfer.
    //! Returns number of bytes that could be served without overdraft.
    /*!
     *  \note Thread affinity: any
     */
    virtual i64 TryAcquireAvailable(i64 count) = 0;

    //! Unconditionally acquires #count bytes for transfer.
    //! This request could easily lead to an overdraft.
    /*!
     *  \note Thread affinity: any
     */
    virtual void Acquire(i64 count) = 0;

    //! Returns |true| if the throttling limit has been exceeded.
    /*!
     *  \note Thread affinity: any
     */
    virtual bool IsOverdraft() = 0;

    //! Returns total byte count of all waiting requests.
    /*!
     *  \note Thread affinity: any
     */
    virtual i64 GetQueueTotalCount() const = 0;

    //! Returns estimated duration to drain current request queue.
    /*!
     *  \note Thread affinity: any
     */
    virtual TDuration GetEstimatedOverdraftDuration() const = 0;
};

DEFINE_REFCOUNTED_TYPE(IThroughputThrottler)

////////////////////////////////////////////////////////////////////////////////

//! Enables dynamic changes of throttling configuration.
/*!
 *  Thread affinity: any
 */
struct IReconfigurableThroughputThrottler
    : public IThroughputThrottler
{
    //! Updates the configuration.
    virtual void Reconfigure(TThroughputThrottlerConfigPtr config) = 0;

    //! Updates the limit.
    //! See TThroughputThrottlerConfig::Limit.
    virtual void SetLimit(std::optional<double> limit) = 0;
};

DEFINE_REFCOUNTED_TYPE(IReconfigurableThroughputThrottler)

////////////////////////////////////////////////////////////////////////////////

//! Constructs a throttler from #config.
IReconfigurableThroughputThrottlerPtr CreateReconfigurableThroughputThrottler(
    TThroughputThrottlerConfigPtr config,
    const NLogging::TLogger& logger = NLogging::TLogger(),
    const NProfiling::TProfiler& profiler = {});

//! Constructs a throttler from #config and initializes logger and profiler.
IReconfigurableThroughputThrottlerPtr CreateNamedReconfigurableThroughputThrottler(
    TThroughputThrottlerConfigPtr config,
    const TString& name,
    NLogging::TLogger logger,
    NProfiling::TProfiler profiler = {});

//! Returns a throttler that imposes no throughput limit.
IThroughputThrottlerPtr GetUnlimitedThrottler();

//! Returns a throttler that imposes no throughput limit and profiles throughput.
IThroughputThrottlerPtr CreateNamedUnlimitedThroughputThrottler(
    const TString& name,
    NProfiling::TProfiler profiler = {});

//! This throttler is DEPRECATED. Use TFairThrottler instead.
IThroughputThrottlerPtr CreateCombinedThrottler(
    const std::vector<IThroughputThrottlerPtr>& throttlers);

//! This throttler is DEPRECATED. Use TFairThrottler instead.
IThroughputThrottlerPtr CreateStealingThrottler(
    IThroughputThrottlerPtr stealer,
    IThroughputThrottlerPtr underlying);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NConcurrency
