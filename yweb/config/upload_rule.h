#pragma once

#include <yweb/protos/robotzones/uploadrules.pb.h>

#include <library/cpp/charset/ci_string.h>

namespace NZones {

class TUploadRule {

public:
    TUploadRule() {}
    TUploadRule(const char* name, const ui32 code);

    inline const char* GetName() const {
        return Name.data();
    }

    inline ui32 GetCode() const {
        return Code;
    }

private:
    TCiString Name;
    ui32 Code;

};

} //namespace
