#include "backend_creator.h"
#include "yd_backend.h"

namespace NUnifiedAgent {

    TLogYdBackendCreator::TLogYdBackendCreator()
        : TLogBackendCreatorBase("yd_unified_agent")
    {}

    bool TLogYdBackendCreator::Init(const IInitContext&) {
        return true;
    }

    void TLogYdBackendCreator::DoToJson(NJson::TJsonValue&) const {
    }

    THolder <TLogBackend> TLogYdBackendCreator::DoCreateLogBackend() const {
        return NUnifiedAgent::MakeYdLogBackend();
    }

}
