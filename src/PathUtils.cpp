#include "PathUtils.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QLatin1Char>

namespace PathUtils {

QString appDeploymentDirPath()
{
    return QDir::cleanPath(QApplication::applicationDirPath());
}

QString resolvePathFromAppDir(const QString& rawPath)
{
    const QString trimmed = rawPath.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const QFileInfo candidate(trimmed);
    if (candidate.isAbsolute()) {
        return QDir::cleanPath(candidate.absoluteFilePath());
    }

    return QDir::cleanPath(QDir(appDeploymentDirPath()).filePath(trimmed));
}

QString relativizePathToAppDir(const QString& rawPath)
{
    const QString trimmed = rawPath.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const QFileInfo candidate(trimmed);
    if (!candidate.isAbsolute()) {
        return QDir::cleanPath(trimmed);
    }

    const QDir appDir(appDeploymentDirPath());
    const QString absolutePath = QDir::cleanPath(candidate.absoluteFilePath());
    const QString relativePath = QDir::cleanPath(appDir.relativeFilePath(absolutePath));
    if (!relativePath.startsWith(QStringLiteral(".."))) {
        return relativePath;
    }
    return absolutePath;
}

int exposureUsFromTemplatePath(const QString& path)
{
    const QString marker = QStringLiteral("exposure_");
    const int start = path.indexOf(marker, 0, Qt::CaseInsensitive);
    if (start < 0) {
        return 0;
    }
    const int valueStart = start + marker.size();
    const int valueEnd = path.indexOf(QStringLiteral("us"), valueStart, Qt::CaseInsensitive);
    if (valueEnd <= valueStart) {
        return 0;
    }

    bool ok = false;
    const int exposureUs = path.mid(valueStart, valueEnd - valueStart).toInt(&ok);
    return ok ? exposureUs : 0;
}

int exposureUsFromTemplateDirName(const QString& name)
{
    const QString marker = QStringLiteral("exposure_");
    if (!name.startsWith(marker, Qt::CaseInsensitive) ||
        !name.endsWith(QStringLiteral("us"), Qt::CaseInsensitive)) {
        return 0;
    }
    bool ok = false;
    const int exposureUs = name.mid(marker.size(), name.size() - marker.size() - 2).toInt(&ok);
    return ok ? exposureUs : 0;
}

QString replaceTemplateExposurePath(const QString& path, int exposureUs)
{
    const QString marker = QStringLiteral("exposure_");
    const int start = path.indexOf(marker, 0, Qt::CaseInsensitive);
    if (start < 0) {
        return QString();
    }
    const int valueStart = start + marker.size();
    const int valueEnd = path.indexOf(QStringLiteral("us"), valueStart, Qt::CaseInsensitive);
    if (valueEnd <= valueStart) {
        return QString();
    }

    const QString exposureDir = QStringLiteral("exposure_%1us")
                                    .arg(exposureUs, 7, 10, QLatin1Char('0'));
    QString replaced = path;
    replaced.replace(start, valueEnd + 2 - start, exposureDir);
    return replaced;
}

} // namespace PathUtils
