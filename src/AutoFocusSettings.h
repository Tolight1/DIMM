#pragma once

#include "AutoFocusController.h"

#include <QString>

class QSettings;

class AutoFocusSettings {
public:
    static AutoFocusConfig load(QSettings& settings);
    static void save(QSettings& settings, const AutoFocusConfig& config);
    static QString validationError(const AutoFocusConfig& config);
    static bool manualStartAvailable(bool trackingAvailable);
};
