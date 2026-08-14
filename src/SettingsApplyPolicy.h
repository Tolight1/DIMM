#pragma once

#include "AppConfig.h"

namespace SettingsApplyPolicy {
bool shouldSendPulseGeneratorHardwareFromMainApply(const ConfigChangeSet& changes);
}
