#include "backend_creator.h"

namespace NUnifiedAgent {

    ILogBackendCreator::TFactory::TRegistrator<NUnifiedAgent::TLogYdBackendCreator> TLogYdBackendCreator::Registrar("yd_unified_agent");

}

