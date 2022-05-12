#include "aio_spdk.h"

#include <ydb/library/pdisk_io/spdk_state.h>

namespace NKikimr {
namespace NPDisk {


std::unique_ptr<IAsyncIoContext> CreateAsyncIoContextSpdk(const TString &, ui32) {
    return {};
}

ISpdkState *TIoContextFactorySPDK::CreateSpdkState() const {
    return Singleton<TSpdkStateOSS>();
}

void TIoContextFactorySPDK::DetectFileParameters(const TString &, ui64 &, bool &) const {
    return;
}
} // NPDisk
} // NKikimr
