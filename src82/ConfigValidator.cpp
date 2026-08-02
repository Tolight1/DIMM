#include "ConfigValidator.h"

ConfigValidationResult ConfigValidator::acceptValidatedConfig(const AppConfigDraft& draft)
{
    ConfigValidationResult result;
    result.valid = true;
    result.config = draft.config;
    return result;
}
