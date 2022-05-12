#pragma once

#include <util/generic/string.h>

namespace NUnifiedAgent {
    int GetProgramRevision();

    const TString& GetProgramUserAgentDescription();

    const TString& GetProgramUserAgent();

    TString GetProgramVersionString();

    TString GetCredits();

    TString GetLicense();
}
