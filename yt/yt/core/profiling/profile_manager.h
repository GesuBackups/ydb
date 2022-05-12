#pragma once

#include "public.h"

#include <yt/yt/core/actions/public.h>

#include <yt/yt/core/yson/string.h>

#include <yt/yt/core/ytree/convert.h>

#include <yt/yt/library/profiling/resource_tracker/resource_tracker.h>

namespace NYT::NProfiling {

////////////////////////////////////////////////////////////////////////////////

//! Represents a sample that was enqueued to the profiler but did not
//! reach the storage yet.
struct TQueuedSample
{
    TCpuInstant Time = -1;
    NYPath::TYPath Path;
    TValue Value = -1;
    TTagIdList TagIds;
    EMetricType MetricType;
};

////////////////////////////////////////////////////////////////////////////////

//! A pre-registered key-value pair used to mark samples.
struct TStringTag
{
    TString Key;
    TString Value;
};

////////////////////////////////////////////////////////////////////////////////

//! A backend that controls all profiling activities.
/*!
 *  This class is a singleton, call #Get to obtain an instance.
 *
 *  Processing happens in a background thread that maintains
 *  a queue of all incoming (queued) samples and distributes them into buckets.
 *
 *  This thread also provides a invoker for executing various callbacks.
 */
class TProfileManager
{
public:
    ~TProfileManager();

    //! Returns the singleton instance.
    static TProfileManager* Get();

    //! Configure profiling.
    void Configure(const TProfileManagerConfigPtr& config);

    //! Starts profiling.
    /*!
     *  No samples are collected before this method is called.
     */
    void Start();

    //! Stops the profiling system.
    /*!
     *  After this call #Enqueue has no effect.
     */
    void Stop();

    //! Enqueues a new sample for processing.
    void Enqueue(const TQueuedSample& sample);

    //! Returns the invoker associated with the profiler thread.
    IInvokerPtr GetInvoker() const;

    //! Returns the root of the tree with buckets.
    /*!
     *  The latter must only be accessed from the invoker returned by #GetInvoker.
     */
    NYTree::IMapNodePtr GetRoot() const;

    //! Returns a thread-safe service representing the tree with buckets.
    NYTree::IYPathServicePtr GetService() const;

    //! Registers a tag and returns its unique id.
    TTagId RegisterTag(const TStringTag& tag);

    TStringTag LookupTag(TTagId tag);

    //! Registers a tag and returns its unique id.
    template <class T>
    TTagId RegisterTag(const TString& key, const T& value);

    //! Registers or updates a tag that is attached to every sample generated in this process.
    void SetGlobalTag(TTagId id);

    TResourceTrackerPtr GetResourceTracker() const;

private:
    TProfileManager();

    Y_DECLARE_SINGLETON_FRIEND();

    class TImpl;
    const TIntrusivePtr<TImpl> Impl_;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NProfiling

template <>
struct TSingletonTraits<NYT::NProfiling::TProfileManager>
{
    enum
    {
        Priority = 2047
    };
};

#define PROFILE_MANAGER_INL_H_
#include "profile_manager-inl.h"
#undef PROFILE_MANAGER_INL_H_
