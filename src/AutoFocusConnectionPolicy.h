#pragma once

#include "AutoFocusController.h"

inline bool shouldOpenFocuserAfterAutoFocusEnable(const AutoFocusConfig& previousConfig,
                                                  const AutoFocusConfig& requestedConfig,
                                                  int cameraIndex,
                                                  bool focuserOpened) noexcept
{
    return cameraIndex >= 0 &&
           cameraIndex < static_cast<int>(requestedConfig.cameraEnabled.size()) &&
           !previousConfig.masterEnabled && requestedConfig.masterEnabled &&
           requestedConfig.cameraEnabled[static_cast<std::size_t>(cameraIndex)] &&
           !focuserOpened;
}
