#pragma once

#include <ydb/core/mon/mon.h>
#include <ydb/core/protos/config.pb.h>

namespace NKikimr {

NActors::TMon* MonitoringFactory(NActors::TMon::TConfig, const NKikimrConfig::TAppConfig&);

}
