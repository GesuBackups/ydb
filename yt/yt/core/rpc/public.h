#pragma once

#include <yt/yt/core/actions/callback.h>

#include <yt/yt/core/bus/public.h>

#include <library/cpp/yt/misc/guid.h>

namespace NYT::NRpc {

////////////////////////////////////////////////////////////////////////////////

namespace NProto {

class TReqDiscover;
class TRspDiscover;
class TRequestHeader;
class TResponseHeader;
class TCredentialsExt;

} // namespace NProto

////////////////////////////////////////////////////////////////////////////////

struct TStreamingParameters;
struct TStreamingPayload;
struct TStreamingFeedback;

struct TServiceDescriptor;
struct TMethodDescriptor;

DECLARE_REFCOUNTED_CLASS(TRequestQueue)

using TRequestQueueProvider = TCallback<TRequestQueue*(const NRpc::NProto::TRequestHeader&)>;
using TInvokerProvider = TCallback<IInvokerPtr(const NRpc::NProto::TRequestHeader&)>;

DECLARE_REFCOUNTED_CLASS(TClientRequest)
DECLARE_REFCOUNTED_CLASS(TClientResponse)

template <class TRequestMessage, class TResponse>
class TTypedClientRequest;

class TClientResponse;

template <class TResponseMessage>
class TTypedClientResponse;

template <class TRequestMessage, class TResponseMessage>
class TTypedServiceContext;

struct TServiceId;

struct TAuthenticationContext;
struct TAuthenticationIdentity;
struct TAuthenticationResult;

DECLARE_REFCOUNTED_STRUCT(IClientRequest)
DECLARE_REFCOUNTED_STRUCT(IClientRequestControl)
DECLARE_REFCOUNTED_STRUCT(IClientResponseHandler)
DECLARE_REFCOUNTED_STRUCT(IServer)
DECLARE_REFCOUNTED_STRUCT(IService)
DECLARE_REFCOUNTED_STRUCT(IServiceWithReflection)
DECLARE_REFCOUNTED_STRUCT(IServiceContext)
DECLARE_REFCOUNTED_STRUCT(IChannel)
DECLARE_REFCOUNTED_STRUCT(IThrottlingChannel)
DECLARE_REFCOUNTED_STRUCT(IChannelFactory)
DECLARE_REFCOUNTED_STRUCT(IRoamingChannelProvider)
DECLARE_REFCOUNTED_STRUCT(IAuthenticator)

DECLARE_REFCOUNTED_CLASS(TClientContext)
DECLARE_REFCOUNTED_CLASS(TServiceBase)
DECLARE_REFCOUNTED_CLASS(TChannelWrapper)
DECLARE_REFCOUNTED_CLASS(TStaticChannelFactory)
DECLARE_REFCOUNTED_CLASS(TClientRequestControlThunk)
DECLARE_REFCOUNTED_CLASS(TCachingChannelFactory)

DECLARE_REFCOUNTED_CLASS(TResponseKeeper)

DECLARE_REFCOUNTED_CLASS(TAttachmentsInputStream)
DECLARE_REFCOUNTED_CLASS(TAttachmentsOutputStream)

DECLARE_REFCOUNTED_CLASS(TDynamicChannelPool)
DECLARE_REFCOUNTED_CLASS(TServerAddressPool)

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(THistogramExponentialBounds)
DECLARE_REFCOUNTED_CLASS(THistogramConfig)
DECLARE_REFCOUNTED_CLASS(TServerConfig)
DECLARE_REFCOUNTED_CLASS(TServiceCommonConfig)
DECLARE_REFCOUNTED_CLASS(TServiceConfig)
DECLARE_REFCOUNTED_CLASS(TMethodConfig)
DECLARE_REFCOUNTED_CLASS(TRetryingChannelConfig)
DECLARE_REFCOUNTED_CLASS(TDynamicChannelPoolConfig)
DECLARE_REFCOUNTED_CLASS(TServiceDiscoveryEndpointsConfig)
DECLARE_REFCOUNTED_CLASS(TBalancingChannelConfig)
DECLARE_REFCOUNTED_CLASS(TThrottlingChannelConfig)
DECLARE_REFCOUNTED_CLASS(TThrottlingChannelDynamicConfig)
DECLARE_REFCOUNTED_CLASS(TResponseKeeperConfig)
DECLARE_REFCOUNTED_CLASS(TMultiplexingBandConfig)
DECLARE_REFCOUNTED_CLASS(TDispatcherConfig)
DECLARE_REFCOUNTED_CLASS(TDispatcherDynamicConfig)

////////////////////////////////////////////////////////////////////////////////

using TRequestId = TGuid;
extern const TRequestId NullRequestId;

using TRealmId = TGuid;
extern const TRealmId NullRealmId;

using TMutationId = TGuid;
extern const TMutationId NullMutationId;

extern const TString RootUserName;

using TNetworkId = int;
constexpr TNetworkId DefaultNetworkId = 0;

constexpr int TypicalMessagePartCount = 8;

using TFeatureIdFormatter = const std::function<const TStringBuf*(int featureId)>*;

using TDiscoverRequestHook = TCallback<void(NProto::TReqDiscover*)>;

////////////////////////////////////////////////////////////////////////////////

extern const TString RequestIdAnnotation;
extern const TString EndpointAnnotation;
extern const TString RequestInfoAnnotation;
extern const TString ResponseInfoAnnotation;

extern const TString FeatureIdAttributeKey;
extern const TString FeatureNameAttributeKey;

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EMultiplexingBand,
    ((Default)               (0))
    ((Control)               (1))
    ((Heavy)                 (2))
    ((Interactive)           (3))
);

YT_DEFINE_ERROR_ENUM(
    ((TransportError)               (static_cast<int>(NBus::EErrorCode::TransportError)))
    ((ProtocolError)                (101))
    ((NoSuchService)                (102))
    ((NoSuchMethod)                 (103))
    ((Unavailable)                  (105)) // The server is not capable of serving requests and
                                           // must not receive any more load.
    ((TransientFailure)             (116)) // Similar to Unavailable but indicates a transient issue,
                                           // which can be safely retried.
    ((PoisonPill)                   (106)) // The client must die upon receiving this error.
    ((RequestQueueSizeLimitExceeded)(108))
    ((AuthenticationError)          (109))
    ((InvalidCsrfToken)             (110))
    ((InvalidCredentials)           (111))
    ((StreamingNotSupported)        (112))
    ((UnsupportedClientFeature)     (113))
    ((UnsupportedServerFeature)     (114))
    ((PeerBanned)                   (115)) // The server is explicitly banned and thus must be dropped.
    ((NoSuchRealm)                  (117))
);

DEFINE_ENUM(EMessageFormat,
    ((Protobuf)    (0))
    ((Json)        (1))
    ((Yson)        (2))
);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpc
