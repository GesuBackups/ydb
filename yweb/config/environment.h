#pragma once

#include <util/generic/singleton.h>
#include <utility>

enum eRobotEnvironment {
  eRobotMoscow /* "production" */,
  eRobotTesting /* "testing" */,
  eRobotStaging /* "staging" */,
  eRobotDevel /* "devel" */
};

eRobotEnvironment GetCurrentRobotEnvironment();
bool GetRobotEnvIsProduction();
