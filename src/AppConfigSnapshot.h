#pragma once

#include "AppConfig.h"

#include <QString>

namespace AppConfigSnapshot {

QString initialFileName();
QString changedFileName(int sequence);
QString changeState(const ConfigChangeSet& changes);
QString toUtf8Text(const AppConfig& config);
bool write(const QString& sessionDirectory,
           const QString& relativeFileName,
           const AppConfig& config,
           QString* errorMessage);

} // namespace AppConfigSnapshot
