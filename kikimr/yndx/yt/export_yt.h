#pragma once

#ifndef KIKIMR_DISABLE_YT

#include <ydb/core/tx/datashard/export_iface.h>

namespace NKikimr {
namespace NYndx {

class TYtExport: public NDataShard::IExport {
public:
    explicit TYtExport(const TTask& task, const TTableColumns& columns)
        : Task(task)
        , Columns(columns)
    {
        Y_VERIFY(task.HasYTSettings());
    }

    IActor* CreateUploader(const TActorId& dataShard, ui64 txId) const override;
    IBuffer* CreateBuffer(ui64 rowsLimit, ui64 bytesLimit) const override;

    void Shutdown() const override;

private:
    const TTask Task;
    const TTableColumns Columns;
};

} // NYndx
} // NKikimr

#endif // KIKIMR_DISABLE_YT
