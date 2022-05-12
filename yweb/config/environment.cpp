#include "environment.h"

#include <util/generic/yexception.h>
#include <util/system/env.h>

#include <cstdlib>

static eRobotEnvironment GetCurrentRobotEnvironmentImpl() {
  TString env = GetEnv("ENVIRONMENT");
  if (!env || env == "production")
    return eRobotMoscow;
  else if (env == "testing")
    return eRobotTesting;
  else if (env == "staging")
    return eRobotStaging;
  else if (env == "devel")
    return eRobotDevel;
  else
    ythrow yexception() << "ENVIRONMENT variable is not set correctly. Incorrect value follows: " << env;
}

static bool EnvIsProductionImpl()
{
  eRobotEnvironment currentRobotEnv = GetCurrentRobotEnvironmentImpl();
  return currentRobotEnv == eRobotMoscow;
}

struct TRobotEnvironment {

  const eRobotEnvironment CurrentRobotEnvironment;
  const bool RobotEnvIsProduction;

  TRobotEnvironment()
  : CurrentRobotEnvironment(GetCurrentRobotEnvironmentImpl())
  , RobotEnvIsProduction(EnvIsProductionImpl())
  {}

};

eRobotEnvironment GetCurrentRobotEnvironment() {
  return Singleton<TRobotEnvironment>()->CurrentRobotEnvironment;
}

bool GetRobotEnvIsProduction() {
  return Singleton<TRobotEnvironment>()->RobotEnvIsProduction;
}
