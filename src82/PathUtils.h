#pragma once

#include <QString>

namespace PathUtils {

QString appDeploymentDirPath();
QString resolvePathFromAppDir(const QString& rawPath);
QString relativizePathToAppDir(const QString& rawPath);
int exposureUsFromTemplatePath(const QString& path);
int exposureUsFromTemplateDirName(const QString& name);
QString replaceTemplateExposurePath(const QString& path, int exposureUs);

} // namespace PathUtils
