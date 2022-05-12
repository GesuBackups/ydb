#include "yd_backend.h"

#include <util/system/env.h>

namespace NUnifiedAgent {

    static constexpr const char* DEPLOY_BOX_META_KEY = "deploy_box";
    static constexpr const char* DEPLOY_WORKLOAD_META_KEY = "deploy_workload";
    static constexpr const char* DEPLOY_CONTAINER_ID_META_KEY = "deploy_container_id";
    static constexpr const char* DEPLOY_LOGGER_NAME_META_KEY = "deploy_logger_name";

    static constexpr const char* DEPLOY_LOGS_SECRET_ENV_NAME = "DEPLOY_LOGS_SECRET";
    static constexpr const char* DEPLOY_BOX_ID_ENV_NAME = "DEPLOY_BOX_ID";
    static constexpr const char* DEPLOY_WORKLOAD_ID_ENV_NAME = "DEPLOY_WORKLOAD_ID";
    static constexpr const char* DEPLOY_CONTAINER_ID_ENV_NAME = "DEPLOY_CONTAINER_ID";
    static constexpr const char* DEPLOY_DEFAULT_LOGGER_NAME_ENV_NAME = "DEPLOY_LOGS_DEFAULT_NAME";
    static constexpr const char* DEPLOY_UA_DEFAULT_URI_ENV_NAME = "DEPLOY_LOGS_ENDPOINT";

    TString YdEnvValue(const TString& envName) {
        TString value = GetEnv(envName);
        if (value.empty()) {
            ythrow yexception() << "Yandex deploy env is not specified: " << envName;
        }
        return value;
    }

    THolder<TLogBackend> AsYdLogBackend(const TClientSessionPtr& session) {
        return AsLogBackend(session);
    }

    THolder<TLogBackend> MakeYdLogBackend(const TString& loggerName,
                                          const TClientParameters& parameters,
                                          const TSessionParameters& sessionParameters,
                                          THolder<IRecordConverter> recordConverter) {
        TClientParameters ydClientParameters(parameters);

        if (!ydClientParameters.SharedSecretKey.Defined()) {
            ydClientParameters.SetSharedSecretKey(YdEnvValue(DEPLOY_LOGS_SECRET_ENV_NAME));
        }

        if (ydClientParameters.Uri.empty()) {
            ydClientParameters.Uri = YdEnvValue(DEPLOY_UA_DEFAULT_URI_ENV_NAME);
        }

        TSessionParameters ydSessionParameters(sessionParameters);

        THashMap<TString, TString> emptyMetaParameters;
        THashMap<TString, TString> metaParameters = ydSessionParameters.Meta.GetOrElse(emptyMetaParameters);

        if (!metaParameters.contains(DEPLOY_BOX_META_KEY)) {
            metaParameters[DEPLOY_BOX_META_KEY] = YdEnvValue(DEPLOY_BOX_ID_ENV_NAME);
        }

        if (!metaParameters.contains(DEPLOY_WORKLOAD_META_KEY)) {
            metaParameters[DEPLOY_WORKLOAD_META_KEY] = YdEnvValue(DEPLOY_WORKLOAD_ID_ENV_NAME);
        }

        if (!metaParameters.contains(DEPLOY_CONTAINER_ID_META_KEY)) {
            metaParameters[DEPLOY_CONTAINER_ID_META_KEY] = YdEnvValue(DEPLOY_CONTAINER_ID_ENV_NAME);
        }

        metaParameters[DEPLOY_LOGGER_NAME_META_KEY] = loggerName.empty() ? YdEnvValue(DEPLOY_DEFAULT_LOGGER_NAME_ENV_NAME) : loggerName;
        ydSessionParameters.SetMeta(metaParameters);

        return MakeLogBackend(ydClientParameters,
                              ydSessionParameters,
                              std::move(recordConverter));
    }

    THolder<::TLog> MakeYdLog(const TString& loggerName, const TClientParameters& parameters, const TSessionParameters& sessionParameters, THolder<IRecordConverter> recordConverter) {
        return MakeHolder<::TLog>(MakeYdLogBackend(loggerName, parameters, sessionParameters, std::move(recordConverter)));
    }
}
