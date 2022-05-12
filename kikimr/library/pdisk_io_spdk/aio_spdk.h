#pragma once

#include <ydb/core/util/yverify_stream.h>
#include <ydb/library/pdisk_io/aio.h>

namespace NKikimr {
namespace NPDisk {

std::unique_ptr<IAsyncIoContext> CreateAsyncIoContextSpdk(const TString &path, ui32 pDiskId);
void DetectFileParametersPcie(TString path, ui64 &outDiskSizeBytes, bool &outIsBlockDevice);

struct TIoContextFactorySPDK : IIoContextFactory {
    std::unique_ptr<IAsyncIoContext> CreateAsyncIoContext(const TString &path, ui32 pDiskId, TDeviceMode::TFlags flags,
            TIntrusivePtr<TSectorMap> sectorMap) const override {
        if (flags & TDeviceMode::UseSpdk) {
            return CreateAsyncIoContextSpdk(path, pDiskId);
        } else if (sectorMap) {
            return CreateAsyncIoContextMap(path, pDiskId, sectorMap);
        } else {
            return CreateAsyncIoContextReal(path, pDiskId, flags);
        }
    }

    ISpdkState *CreateSpdkState() const override;

    void DetectFileParameters(const TString &path, ui64 &outDiskSizeBytes, bool &outIsBlockDevice) const override;
};

} // NPDisk
} // NKikimr
