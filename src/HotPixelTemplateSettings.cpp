#include "HotPixelTemplateSettings.h"

#include "ConfigTextUtils.h"

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1Char>
#include <QTextStream>

bool loadHotPixelTemplateSettings(const QString& path, HotPixelTemplateSettings* settings)
{
    if (!settings) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    HotPixelTemplateSettings parsed;
    const QFileInfo configInfo(path);
    QTextStream input(&file);
    while (!input.atEnd()) {
        const QString line = ConfigTextUtils::stripInlineComment(input.readLine());
        if (line.isEmpty()) {
            continue;
        }

        const int equalPos = line.indexOf(QLatin1Char('='));
        if (equalPos <= 0) {
            continue;
        }

        const QString key = line.left(equalPos).trimmed().toLower();
        const QString valueText = line.mid(equalPos + 1).trimmed();
        auto resolveConfigPath = [&configInfo](const QString& rawPath) {
            QFileInfo candidate(rawPath);
            if (candidate.isAbsolute()) {
                return candidate.absoluteFilePath();
            }
            return QFileInfo(configInfo.absoluteDir(), rawPath).absoluteFilePath();
        };

        bool ok = false;
        const double number = valueText.toDouble(&ok);
        if ((key == QStringLiteral("hot_pixel_template_width") ||
             key == QStringLiteral("hot_template_width")) && ok) {
            parsed.width = std::max(0, static_cast<int>(std::lround(number)));
        } else if ((key == QStringLiteral("hot_pixel_template_height") ||
                    key == QStringLiteral("hot_template_height")) && ok) {
            parsed.height = std::max(0, static_cast<int>(std::lround(number)));
        } else if (key == QStringLiteral("camera_a_hot_pixel_mask") ||
                   key == QStringLiteral("camera0_hot_pixel_mask")) {
            parsed.camera0Mask = resolveConfigPath(valueText);
        } else if (key == QStringLiteral("camera_a_hot_pixel_excess") ||
                   key == QStringLiteral("camera0_hot_pixel_excess")) {
            parsed.camera0Excess = resolveConfigPath(valueText);
        } else if (key == QStringLiteral("camera_b_hot_pixel_mask") ||
                   key == QStringLiteral("camera1_hot_pixel_mask")) {
            parsed.camera1Mask = resolveConfigPath(valueText);
        } else if (key == QStringLiteral("camera_b_hot_pixel_excess") ||
                   key == QStringLiteral("camera1_hot_pixel_excess")) {
            parsed.camera1Excess = resolveConfigPath(valueText);
        }
    }

    const bool hasCompleteTemplate =
        parsed.width > 0 &&
        parsed.height > 0 &&
        !parsed.camera0Mask.isEmpty() &&
        !parsed.camera0Excess.isEmpty() &&
        !parsed.camera1Mask.isEmpty() &&
        !parsed.camera1Excess.isEmpty();
    if (!hasCompleteTemplate) {
        return false;
    }

    *settings = parsed;
    return true;
}
