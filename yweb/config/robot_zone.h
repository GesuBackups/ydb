#pragma once

#include <yweb/protos/robotzones/robotzone.pb.h>

#include <library/cpp/charset/ci_string.h>

namespace NZones {


///////////////////
/// robot zones ///
///////////////////

class TRobotZone {

public:
    TRobotZone() {}
    TRobotZone(const NRobotZones::ESelectionRankFormulaId selectionRankFormulaId, const char* name, const ui32 code);

    inline NRobotZones::ESelectionRankFormulaId GetSelectionRankFormulaId() const {
        return SelectionRankFormulaId;
    }

    inline const char* GetName() const {
        return Name.data();
    }

    inline ui32 GetCode() const {
        return Code;
    }

private:
    NRobotZones::ESelectionRankFormulaId SelectionRankFormulaId;
    TCiString Name;
    ui32 Code;

};

} //namespace
