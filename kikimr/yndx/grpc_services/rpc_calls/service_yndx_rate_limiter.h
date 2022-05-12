#pragma once

#include <memory>

namespace NKikimr {
namespace NGRpcService {

class IRequestOpCtx;
class IFacilityProvider;

void DoCreateYndxRateLimiterResource(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);
void DoAlterYndxRateLimiterResource(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);
void DoDropYndxRateLimiterResource(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);
void DoListYndxRateLimiterResources(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);
void DoDescribeYndxRateLimiterResource(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);
void DoAcquireYndxRateLimiterResource(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);

}
}
