#include "aio_spdk.h"
#include "spdk_state.h"

#include <ydb/library/pdisk_io/aio.h>
#include <ydb/library/pdisk_io/buffers.h>
#include <ydb/library/pdisk_io/file_params.h>
#include <ydb/library/pdisk_io/spdk_state.h>

#include <contrib/libs/libaio/libaio.h>
#include <contrib/libs/spdk/include/spdk/nvme.h>

#include <linux/fs.h>
#include <sys/ioctl.h>
#include <util/stream/format.h>
#include <util/system/file.h>

namespace NKikimr {
namespace NPDisk {

class TCallbackContext;

struct TAsyncIoOperationSpdk : IAsyncIoOperation {
    void* Buf = nullptr;
    ui64 Size = 0;
    ui32 Offset = 0;
    void* Cookie;
    TReqId ReqId;
    IAsyncIoOperation::EType Type = IAsyncIoOperation::EType::PRead;
    ICallback *Callback = nullptr;
    NWilson::TTraceId TraceId;

    TAsyncIoOperationSpdk() = default;

    TAsyncIoOperationSpdk(void *cookie, TReqId reqId, NWilson::TTraceId *traceId)
        : Cookie(cookie)
        , ReqId(reqId)
        , TraceId(traceId ? std::move(*traceId) : NWilson::TTraceId())
    {}

    ~TAsyncIoOperationSpdk() override {
    }

    static void call_back(void *cookie, const struct spdk_nvme_cpl *cpl) {
        Y_UNUSED(cpl);
        TAsyncIoOperationResult result;
        result.Operation = static_cast<TAsyncIoOperationSpdk*>(cookie);
        result.Result = EIoResult::Ok;
        result.Operation->ExecCallback(&result);
    }

    void* GetCookie() override {
        return Cookie;
    }

    NWilson::TTraceId *GetTraceIdPtr() override {
        return &TraceId;
    }

    void* GetData() override {
        return Buf;
    }

    ui64 GetOffset() override {
        return Offset;
    };

    ui64 GetSize() override {
        return Size;
    };

    TReqId GetReqId() override {
        return ReqId;
    }

    EType GetType() override {
        return Type;
    };

    void SetCallback(ICallback *callback) override {
        Callback = callback;
    }

    void ExecCallback(TAsyncIoOperationResult *result) override {
        Callback->Exec(result);
    }
};

class TAsyncIoContextSpdk : public IAsyncIoContext {
    TActorSystem *ActorSystem;
    TPool<TAsyncIoOperationSpdk, 1024> Pool;

    struct spdk_nvme_ctrlr *NvmeCtrlr = nullptr;
    struct spdk_nvme_qpair *QueuePair = nullptr;
    struct spdk_nvme_ns *NvmeNs = nullptr;
    const ui32 NvmeNsId = 1;
    static constexpr ui64 BlockSize = 4ul << 10;
    int LastErrno = 0;
    ui32 SectorSize = 0;

    ISpdkState *SpdkState = nullptr;

    TPDiskDebugInfo PDiskInfo;
public:

    TAsyncIoContextSpdk(const TString &path, ui32 pDiskId)
        : ActorSystem(nullptr)
        , PDiskInfo(path, pDiskId, "SPDK")
    {}

    ~TAsyncIoContextSpdk() {
    }

    void InitializeMonitoring(TPDiskMon &/*mon*/) override {
        //Pool.InitializeMonitoring(mon);
    }

    IAsyncIoOperation* CreateAsyncIoOperation(void* cookie, TReqId reqId, NWilson::TTraceId *traceId) override {
        void *p = Pool.Pop();
        IAsyncIoOperation *operation = new (p) TAsyncIoOperationSpdk(cookie, reqId, traceId);
        return operation;
    }

    void DestroyAsyncIoOperation(IAsyncIoOperation* operation) override {
        Pool.Push(static_cast<TAsyncIoOperationSpdk*>(operation));
    }

    i64 GetEvents(ui64 minEvents, ui64 maxEvents, TAsyncIoOperationResult *events, TDuration timeout) override {
        Y_VERIFY(minEvents == 0, "minEvents must be zero, because SPDK GetEvents processes completions that are ready"
                " at the time of this function call");
        Y_UNUSED(events);
        Y_UNUSED(timeout);
        return spdk_nvme_qpair_process_completions(QueuePair, maxEvents);
    }

    void PrepareImpl(IAsyncIoOperation *op, void *data, size_t size, size_t offset, IAsyncIoOperation::EType type) {
        auto req = static_cast<TAsyncIoOperationSpdk*>(op);
        req->Buf = data;
        req->Size = size;
        req->Offset = offset;
        req->Type = type;
    }

    void PreparePRead(IAsyncIoOperation *op, void *destination, size_t size, size_t offset) override {
        PrepareImpl(op, destination, size, offset, IAsyncIoOperation::EType::PRead);
    }

    void PreparePWrite(IAsyncIoOperation *op, const void *source, size_t size, size_t offset) override {
        PrepareImpl(op, const_cast<void*>(source), size, offset, IAsyncIoOperation::EType::PWrite);
    }

    void PreparePTrim(IAsyncIoOperation *op, size_t size, size_t offset) override {
        PrepareImpl(op, nullptr, size, offset, IAsyncIoOperation::EType::PTrim);
    }

    bool DoTrim(IAsyncIoOperation *op) override {
        Y_UNUSED(op);
        return false;
    }

    EIoResult Setup(ui64 maxEvents, bool doLock) override {
        Y_UNUSED(maxEvents);
        Y_UNUSED(doLock);

        SpdkState = CreateSpdkStateSpdk();

        Y_VERIFY_S(PDiskInfo.Path.StartsWith("PCIe:"), "Path to SPDK NVMe device must starts with \"PCIe:\","
                " but path# \"" << PDiskInfo.Path << "\"");
        TStringStream str;
        auto colon = PDiskInfo.Path.find_first_of(':');
        Y_VERIFY(colon <= PDiskInfo.Path.size());
        str << "trtype:" << "PCIe" << " " << "traddr:" << PDiskInfo.Path.substr(colon + 1);
        struct spdk_nvme_transport_id trid;

        int ret = spdk_nvme_transport_id_parse(&trid, str.Str().c_str());
        Y_VERIFY_S(ret == 0, PDiskInfo << " error in transport_id parsing, transport_id_string# "
                << str.Str() << " strerror# " << strerror(-ret));

        NvmeCtrlr = spdk_nvme_connect(&trid, NULL, 0);
        Y_VERIFY_S(NvmeCtrlr, PDiskInfo << " error in NVMe device connection");

        struct spdk_nvme_io_qpair_opts qpair_opts;
        spdk_nvme_ctrlr_get_default_io_qpair_opts(NvmeCtrlr, &qpair_opts, sizeof(qpair_opts));

        QueuePair = spdk_nvme_ctrlr_alloc_io_qpair(NvmeCtrlr, &qpair_opts, sizeof(qpair_opts));
        Y_VERIFY_S(QueuePair, PDiskInfo << " error in qpair allocation for NVMe device");

        Y_VERIFY_S(spdk_nvme_ctrlr_get_num_ns(NvmeCtrlr) > 0,
                PDiskInfo << " NVMe device must have more than zero namespaces");

        NvmeNs = spdk_nvme_ctrlr_get_ns(NvmeCtrlr, NvmeNsId);

        Y_VERIFY_S(NvmeNs, PDiskInfo << " error in NVMe namespace connection, ns_id# " << NvmeNsId);

        SectorSize = spdk_nvme_ns_get_sector_size(NvmeNs);

        Y_VERIFY_S(SectorSize > 0, PDiskInfo << " NVMe device sector size must be greater than zero");
        return EIoResult::Ok;
    }

    EIoResult Destroy() override {
        int ret;
        ret = spdk_nvme_qpair_process_completions(QueuePair, 0);
        Y_VERIFY_S(ret >= 0, PDiskInfo << " error in processing completions");

        ret = spdk_nvme_ctrlr_process_admin_completions(NvmeCtrlr);
        Y_VERIFY_S(ret >= 0, PDiskInfo << " error in processing admin completions");
        spdk_nvme_ctrlr_free_io_qpair(QueuePair);
        spdk_nvme_detach(NvmeCtrlr);
        return EIoResult::Ok;
    }

    EIoResult Submit(IAsyncIoOperation *op, ICallback *callback) override {
        auto req = static_cast<TAsyncIoOperationSpdk*>(op);
        if (ActorSystem) {
            //WILSON_TRACE(*ActorSystem, op->GetTraceIdPtr(), AsyncIoInQueue);
        }

        if (op->GetType() == IAsyncIoOperation::EType::PWrite) {
            //PDISK_FAIL_INJECTION(1);
        }

#if defined(__has_feature)
#    if __has_feature(thread_sanitizer)
        //
        // Thread Sanitizer does not consider io_submit / io_getevents synchronization.
        //
        AtomicStore((char*)op, *(char*)op);
#    endif
#endif

        op->SetCallback(callback);

        int ret = 0;

        switch(req->GetType()) {
            case IAsyncIoOperation::EType::PRead:
                ret = spdk_nvme_ns_cmd_read(NvmeNs, QueuePair, req->GetData(), req->GetOffset() / SectorSize,
                        req->GetSize() / SectorSize, TAsyncIoOperationSpdk::call_back, op, 0);
                break;
            case IAsyncIoOperation::EType::PWrite:
                ret = spdk_nvme_ns_cmd_write(NvmeNs, QueuePair, req->GetData(), req->GetOffset() / SectorSize,
                        req->GetSize() / SectorSize, TAsyncIoOperationSpdk::call_back, op, 0);
                break;
            default:
                Y_FAIL_S(PDiskInfo << " unexpected IO request type# " << i64(req->GetType()));
                break;
        }
        if (ret != 0) {
            switch (-ret) {
                case ENOMEM: return EIoResult::OutOfMemory;
                default: Y_FAIL_S(PDiskInfo << " unexpected error in command submission, error# " << -ret
                                 << " strerror# " << strerror(-ret));
            }
        } else {
            return EIoResult::Ok;
        }
    }

    void SetActorSystem(TActorSystem *actorSystem) override {
        ActorSystem = actorSystem;
    }

    TString GetPDiskInfo() override {
        return PDiskInfo.Str();
    }

    int GetLastErrno() override {
        return LastErrno;
    }

    TFileHandle *GetFileHandle() override {
        return nullptr;
    }
};

std::unique_ptr<IAsyncIoContext> CreateAsyncIoContextSpdk(const TString &path, ui32 pDiskId) {
    return std::make_unique<TAsyncIoContextSpdk>(path, pDiskId);
}

ISpdkState *TIoContextFactorySPDK::CreateSpdkState() const {
    return CreateSpdkStateSpdk();
}

void TIoContextFactorySPDK::DetectFileParameters(const TString &path, ui64 &outDiskSizeBytes, bool &outIsBlockDevice) const {
    if (path.StartsWith("PCIe")) {
        DetectFileParametersPcie(path, outDiskSizeBytes, outIsBlockDevice);
    } else {
        ::NKikimr::DetectFileParameters(path, outDiskSizeBytes, outIsBlockDevice);
    }
}

} // NPDisk
} // NKikimr
