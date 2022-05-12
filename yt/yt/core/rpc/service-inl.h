#ifndef SERVICE_INL_H_
#error "Direct inclusion of this file is not allowed, include service.h"
// For the sake of sane code completion.
#include "service.h"
#endif

#include "helpers.h"

#include <yt/yt/core/misc/format.h>

namespace NYT::NRpc {

////////////////////////////////////////////////////////////////////////////////

template <class... TArgs>
void IServiceContext::SetRequestInfo(const char* format, TArgs&&... args)
{
    if (GetLogger().IsLevelEnabled(NLogging::ELogLevel::Debug)) {
        SetRawRequestInfo(Format(format, std::forward<TArgs>(args)...), false);
    }
}

template <class... TArgs>
void IServiceContext::SetIncrementalRequestInfo(const char* format, TArgs&&... args)
{
    if (GetLogger().IsLevelEnabled(NLogging::ELogLevel::Debug)) {
        SetRawRequestInfo(Format(format, std::forward<TArgs>(args)...), true);
    }
}

template <class... TArgs>
void IServiceContext::SetResponseInfo(const char* format, TArgs&&... args)
{
    if (GetLogger().IsLevelEnabled(NLogging::ELogLevel::Debug)) {
        SetRawResponseInfo(Format(format, std::forward<TArgs>(args)...), false);
    }
}

template <class... TArgs>
void IServiceContext::SetIncrementalResponseInfo(const char* format, TArgs&&... args)
{
    if (GetLogger().IsLevelEnabled(NLogging::ELogLevel::Debug)) {
        SetRawResponseInfo(Format(format, std::forward<TArgs>(args)...), true);
    }
}

namespace NDetail {

bool IsClientFeatureSupported(const IServiceContext* context, int featureId);
void ThrowUnsupportedClientFeature(int featureId, TStringBuf featureName);

} // namespace NDetail

template <class E>
bool IServiceContext::IsClientFeatureSupported(E featureId) const
{
    return NDetail::IsClientFeatureSupported(this, FeatureIdToInt(featureId));
}

template <class E>
void IServiceContext::ValidateClientFeature(E featureId) const
{
    auto intFeatureId = FeatureIdToInt(featureId);
    if (!NDetail::IsClientFeatureSupported(this, intFeatureId)) {
        NDetail::ThrowUnsupportedClientFeature(intFeatureId, *TEnumTraits<E>::FindLiteralByValue(featureId));
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpc
