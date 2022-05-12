#pragma once

#include <memory>

namespace NKikimr::NGRpcService {

    class IRequestOpCtx;
    class IFacilityProvider;

    void DoCreateVolumeYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);
    void DoDropVolumeYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);
    void DoListLocalPartitionsYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);

    void DoAcquireLockYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);
    void DoExecuteTransactionYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);
    void DoReadYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);
    void DoReadRangeYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);
    void DoListRangeYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);
    void DoGetStorageChannelStatusYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&);

} // NKikimr::NGRpcService
