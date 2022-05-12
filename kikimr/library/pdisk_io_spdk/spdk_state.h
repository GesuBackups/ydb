#pragma once

#include <ydb/core/util/yverify_stream.h>
#include <ydb/library/pdisk_io/spdk_state.h>

namespace NKikimr::NPDisk {

ISpdkState *CreateSpdkStateSpdk();

}
