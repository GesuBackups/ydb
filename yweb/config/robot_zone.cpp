#include "robot_zone.h"
#include <library/cpp/charset/ci_string.h>
#include <util/generic/yexception.h>

namespace NZones {

TRobotZone::TRobotZone(const NRobotZones::ESelectionRankFormulaId selectionRankFormulaId, const char* name, const ui32 code)
    : SelectionRankFormulaId(selectionRankFormulaId)
    , Name(name)
    , Code(code)
{}

} //namespace
