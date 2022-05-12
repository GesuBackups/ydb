#pragma once

#ifndef KIKIMR_DISABLE_S3_OPS

#include <ydb/core/tx/datashard/export_s3.h>

namespace NKikimr {
namespace NYndx {

class TS3Export: public NDataShard::TS3Export {
public:
    using NDataShard::TS3Export::TS3Export;

    IActor* CreateUploader(const TActorId& dataShard, ui64 txId) const override;
};

} // NYndx
} // NKikimr

#endif // KIKIMR_DISABLE_S3_OPS
