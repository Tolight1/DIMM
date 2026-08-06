#pragma once

#include "AppConfig.h"

#include <QString>

struct AppConfigDraft {
    AppConfig config;
};

struct ConfigValidationResult {
    bool valid = false;
    QString message;
    AppConfig config;
};

namespace ConfigValidator {
ConfigValidationResult acceptValidatedConfig(const AppConfigDraft& draft);
}
