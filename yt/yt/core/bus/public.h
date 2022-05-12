#pragma once

#include <yt/yt/core/misc/public.h>

namespace NYT::NBus {

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_STRUCT(IBus)
DECLARE_REFCOUNTED_STRUCT(IMessageHandler)
DECLARE_REFCOUNTED_STRUCT(IBusClient)
DECLARE_REFCOUNTED_STRUCT(IBusServer)

struct TTcpDispatcherStatistics;

DECLARE_REFCOUNTED_CLASS(TTcpDispatcherConfig)
DECLARE_REFCOUNTED_CLASS(TTcpDispatcherDynamicConfig)
DECLARE_REFCOUNTED_CLASS(TTcpBusConfig)
DECLARE_REFCOUNTED_CLASS(TTcpBusServerConfig)
DECLARE_REFCOUNTED_CLASS(TTcpBusClientConfig)

DECLARE_REFCOUNTED_STRUCT(TTcpDispatcherCounters)

using TTosLevel = int;
constexpr int DefaultTosLevel = 0;
constexpr int BlackHoleTosLevel = -1;

constexpr size_t MaxMessagePartCount = 1 << 28;
constexpr size_t MaxMessagePartSize = 1_GB;

DEFINE_ENUM(EDeliveryTrackingLevel,
    (None)
    (ErrorOnly)
    (Full)
);

YT_DEFINE_ERROR_ENUM(
    ((TransportError)               (100))
);

extern const TString DefaultNetworkName;
extern const TString LocalNetworkName;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NBus

