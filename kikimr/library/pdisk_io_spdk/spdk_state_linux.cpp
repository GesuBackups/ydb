#include "spdk_state.h"

#include <contrib/libs/dpdk/config/rte_config.h>
#include <contrib/libs/spdk/include/spdk/nvme.h>

#include <util/generic/singleton.h>
#include <util/system/mutex.h>
#include <util/system/thread.h>

namespace NKikimr::NPDisk {

class TSpdkStateSpdk : public ISpdkState {
    TMutex EnvInitMtx;
    // Don't use 0! In that case LauchThread goes to infinite sleep
    TAtomic CurrentFreeCore = 1;

public:
    TSpdkStateSpdk() {
        auto initFunc = [](void *) -> void * {
            struct spdk_env_opts opts;
            spdk_env_opts_init(&opts);
            opts.name = "pdisk's DPDK EAL";
            opts.core_mask = "0ffff0";
            // DPDK huge memory size in MB
            opts.mem_size = 1024;

            opts.main_core = 4;
            int ret = spdk_env_init(&opts);
            Y_VERIFY_S(ret == 0, strerror(-ret));
            return nullptr;
        };

        TThread initThread(initFunc, nullptr);
        initThread.Start();
        initThread.Join();
    }

    void LaunchThread(int (*fn)(void *), void *cookie) override {
        TGuard<TMutex> guard(EnvInitMtx);
        ui32 i = spdk_env_get_first_core();
        const TAtomicBase currentCore = AtomicGetAndIncrement(CurrentFreeCore);
        for (ui32 core = 0; core < currentCore; core++) {
            i = spdk_env_get_next_core(i);
        }
        Y_VERIFY(i < RTE_MAX_LCORE);
        int ret = spdk_env_thread_launch_pinned(i, fn, cookie);
        Y_VERIFY_S(ret == 0, strerror(-ret));
    }

    void WaitAllThreads() override {
        AtomicSet(CurrentFreeCore, 1);
        spdk_env_thread_wait_all();
    }

    ui8 *Malloc(ui64 size, ui32 align) override {
        static ui64 totalyAllocated = 0;
        void *buf = spdk_dma_malloc(size, align, NULL);
        Y_VERIFY_S(buf != NULL, "Error in spdk_dma_malloc, size# " << size <<" align# " << align);
        totalyAllocated += size;
        return static_cast<ui8*>(buf);
    }

    void Free(ui8 *buf) override {
        spdk_dma_free(buf);
    }
};

ISpdkState *CreateSpdkStateSpdk() {
    return Singleton<TSpdkStateSpdk>();
}
}
