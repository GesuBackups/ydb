#include "langregion.h"
#include "environment.h"

#include <util/generic/yexception.h>

static ELangRegion GetDefaultLangRegionImpl() {

  eRobotEnvironment currentRobotEnvironment = GetCurrentRobotEnvironment();
  if (currentRobotEnvironment == eRobotMoscow ||
      currentRobotEnvironment == eRobotTesting ||
      currentRobotEnvironment == eRobotStaging ||
      currentRobotEnvironment == eRobotDevel)
    return ELR_RU;
  else
    ythrow yexception() << "GetDefaultLangRegion doesn't know which lang region is default for robot environment: " << (ui32)currentRobotEnvironment;
}

struct TDefaultLangRegion {

  const ELangRegion DefaultLangRegion;

  TDefaultLangRegion()
  : DefaultLangRegion(GetDefaultLangRegionImpl())
  {}

};

ELangRegion GetDefaultLangRegion() {
  return Singleton<TDefaultLangRegion>()->DefaultLangRegion;
}
