#include "SettingsApplyPolicy.h"

namespace SettingsApplyPolicy {

bool shouldSendPulseGeneratorHardwareFromMainApply(const ConfigChangeSet& changes)
{
    Q_UNUSED(changes);
    return false;
}

} // namespace SettingsApplyPolicy
