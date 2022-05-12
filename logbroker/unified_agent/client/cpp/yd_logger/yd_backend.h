#pragma once

#include <logbroker/unified_agent/client/cpp/client.h>
#include <logbroker/unified_agent/client/cpp/logger/backend.h>

#include <library/cpp/logger/backend.h>
#include <library/cpp/logger/log.h>

namespace NUnifiedAgent {
    THolder<TLogBackend> AsYdLogBackend(const TClientSessionPtr& session);

    THolder<TLogBackend> MakeYdLogBackend(const TString& loggerName = "",
                                          const TClientParameters& parameters = TClientParameters(""),
                                          const TSessionParameters& sessionParameters = {},
                                          THolder<IRecordConverter> recordConverter = {});

    THolder<::TLog> MakeYdLog(const TString& loggerName = "",
                              const TClientParameters& parameters = TClientParameters(""),
                              const TSessionParameters& sessionParameters = {},
                              THolder<IRecordConverter> recordConverter = {});
}
