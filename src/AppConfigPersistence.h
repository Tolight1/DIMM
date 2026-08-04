#pragma once

#include "AppConfig.h"

namespace AppConfigPersistence {

AppConfig load(const AppConfig& defaults);
void save(const AppConfig& config);

}
