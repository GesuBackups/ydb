#include "log_backend.h"
#include <ydb/core/base/counters.h>
#include <logbroker/unified_agent/client/cpp/logger/backend.h>
#include <util/system/getpid.h>

namespace NKikimr {

TAutoPtr<TLogBackend> TLogBackendFactoryWithUnifiedAgent::CreateLogBackend(
        const TKikimrRunConfig& runConfig,
        NMonitoring::TDynamicCounterPtr counters)
{
    TAutoPtr<TLogBackend> logBackend = CreateLogBackendFromLogConfig(runConfig);

    if (runConfig.AppConfig.HasLogConfig() && runConfig.AppConfig.GetLogConfig().HasUAClientConfig()) {
        const auto& uaClientConfig = runConfig.AppConfig.GetLogConfig().GetUAClientConfig();
        auto uaCounters = GetServiceCounters(counters, "utils")->GetSubgroup("subsystem", "ua_client");
        auto parameters = NUnifiedAgent::TClientParameters(uaClientConfig.GetUri())
            .SetCounters(uaCounters)
            .SetMaxInflightBytes(uaClientConfig.GetMaxInflightBytes());
        if (uaClientConfig.HasSharedSecretKey()) {
            parameters.SetSharedSecretKey(uaClientConfig.GetSharedSecretKey());
        }
        if (uaClientConfig.HasGrpcReconnectDelayMs()) {
            parameters.SetGrpcReconnectDelay(TDuration::MilliSeconds(uaClientConfig.GetGrpcReconnectDelayMs()));
        }
        if (uaClientConfig.HasGrpcSendDelayMs()) {
            parameters.SetGrpcSendDelay(TDuration::MilliSeconds(uaClientConfig.GetGrpcSendDelayMs()));
        }
        if (uaClientConfig.HasGrpcMaxMessageSize()) {
            parameters.SetGrpcMaxMessageSize(uaClientConfig.GetGrpcMaxMessageSize());
        }
        if (uaClientConfig.HasClientLogFile()) {
            TLog log(uaClientConfig.GetClientLogFile(),
                     static_cast<ELogPriority>(uaClientConfig.GetClientLogPriority()));
            parameters.SetLog(log);
        }

        auto sessionParameters = NUnifiedAgent::TSessionParameters();
        sessionParameters.Meta.ConstructInPlace();
        (*sessionParameters.Meta)["_pid"] = ToString(GetPID());
        if (uaClientConfig.HasLogName()) {
            (*sessionParameters.Meta)["_log_name"] = uaClientConfig.GetLogName();
        }

        TAutoPtr<TLogBackend> uaLogBackend = MakeLogBackend(parameters, sessionParameters).Release();
        logBackend = logBackend ? NActors::CreateCompositeLogBackend({logBackend, uaLogBackend}) : uaLogBackend;
    }

    if (logBackend) {
        return logBackend;
    } else {
        return NActors::CreateStderrBackend();
    }
}

} // NKikimr

