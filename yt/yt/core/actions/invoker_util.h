#pragma once

#include "public.h"

#include <yt/yt/core/concurrency/public.h>
#include <yt/yt/core/concurrency/scheduler_api.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

//! Returns the synchronous-ish invoker that defers recurrent action invocation.
/*!
 *  The invoker's |Invoke| method invokes the closure immediately unless invoking
 *  code is already running within an action in a sync invoker. In latter case all
 *  subsequent actions are put into the deferred action queue and are executed one
 *  by one in synchronous manner after completion of the outermost action.
 *  This is quite similar to BFS over the recursion tree.
 *
 *  Such implementation ensures that Subscribe chains over futures from sync invoker
 *  do not lead to an unbounded recursion. Note that the invocation order is slightly
 *  different from the "truly synchronous" invoker that always invokes actions synchronously,
 *  which corresponds to DFS over the recursion tree.
 */
IInvokerPtr GetSyncInvoker();

//! Returns the null invoker, i.e. the invoker whose |Invoke|
//! method does nothing.
IInvokerPtr GetNullInvoker();

//! Returns a special per-process invoker that handles all asynchronous finalization
//! activities (fiber unwinding, abandoned promise cancelation etc).
/*!
 *  This call may return a null invoker (cf. #GetNullInvoker) if the finalizer thread has been shut down.
 *  This is the caller's responsibility to handle such a case gracefully.
 */
IInvokerPtr GetFinalizerInvoker();

//! Tries to invoke #onSuccess via #invoker.
//! If the invoker discards the callback without executing it then
//! #onCancel is run.
void GuardedInvoke(
    const IInvokerPtr& invoker,
    TClosure onSuccess,
    TClosure onCancel);

////////////////////////////////////////////////////////////////////////////////
// Provides a way to work with the current invoker.

IInvokerPtr GetCurrentInvoker();
void SetCurrentInvoker(IInvokerPtr invoker);

//! Swaps the current active invoker with a provided one.
class TCurrentInvokerGuard
    : NConcurrency::TContextSwitchGuard
{
public:
    explicit TCurrentInvokerGuard(IInvokerPtr invoker);
    ~TCurrentInvokerGuard();

private:
    void Restore();

    bool Active_;
    IInvokerPtr SavedInvoker_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
