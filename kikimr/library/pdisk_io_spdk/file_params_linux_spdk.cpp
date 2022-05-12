#include "spdk_state.h"

#include <ydb/library/pdisk_io/spdk_state.h>
#include <contrib/libs/spdk/include/spdk/nvme.h>

#include <util/generic/yexception.h>

//#include <linux/fs.h>
//#include <regex>
//#include <sys/ioctl.h>
//#include <sys/stat.h>
//#include <sys/types.h>
//#include <unistd.h>

namespace NKikimr {
namespace NPDisk {

void DetectFileParametersPcie(TString path, ui64 &outDiskSizeBytes, bool &outIsBlockDevice) {
    Y_VERIFY(path.StartsWith("PCIe"));
    auto colon = path.find_first_of(':');
    if (colon >= path.size()) {
        ythrow yexception() << "Error in parsing path# \"" << path << "\", where must be a colon in NVMe SPDK path";
    }
    TStringStream tr_id_str;
    tr_id_str << "trtype:" << "PCIe" << " " << "traddr:" << path.substr(colon + 1);

    TStringStream errorStr;
    errorStr << "path # \"" << path << "\", transport_id string# \"" << tr_id_str.Str() << "\"";

    // should initialize State if not yet
    CreateSpdkStateSpdk();
    struct spdk_nvme_transport_id trid;
    int ret = spdk_nvme_transport_id_parse(&trid, tr_id_str.Str().c_str());
    if (ret != 0) {
        ythrow yexception() << errorStr.Str() << " Error in parsing transport id, error#" << strerror(-ret);
    }

    struct spdk_nvme_ctrlr *nvmeCtrlr;
    nvmeCtrlr = spdk_nvme_connect(&trid, NULL, 0);
    if (!nvmeCtrlr) {
        ythrow yexception() << errorStr.Str() << " Error in nvme connection";
    }

    if (spdk_nvme_ctrlr_get_num_ns(nvmeCtrlr) <= 0) {
        ythrow yexception() << errorStr.Str() << " Not enough namespaces in nvme device";
    }

    struct spdk_nvme_ns *nvmeNs;
    // Documentation: Namespaces are numbered from 1 to the total number of namespaces
    const ui32 spdk_nvme_ns_id = 1;
    nvmeNs = spdk_nvme_ctrlr_get_ns(nvmeCtrlr, spdk_nvme_ns_id);
    if (!nvmeNs) {
        ythrow yexception() << errorStr.Str() << " Error getting namespace for device, namespace_id# "
            << spdk_nvme_ns_id;
    }

    outDiskSizeBytes = spdk_nvme_ns_get_size(nvmeNs);
    outIsBlockDevice = true;

    spdk_nvme_detach(nvmeCtrlr);
    return;
}

}
}
