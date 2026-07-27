#include "InitialStarDetectionConfig.h"

#include "ConfigTextUtils.h"
#include "ImageUtils.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1Char>
#include <QString>
#include <QTextStream>

namespace {

InitialStarDetectionConfig loadInitialStarDetectionConfig()
{
    InitialStarDetectionConfig config;
    const QString appThresholdPath =
        QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("threshold.txt"));
    const QString cwdThresholdPath = QDir::current().filePath(QStringLiteral("threshold.txt"));
    const QString path = QFileInfo::exists(appThresholdPath)
                             ? appThresholdPath
                             : (QFileInfo::exists(cwdThresholdPath) ? cwdThresholdPath : QString());
    if (path.isEmpty()) {
        return config;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return config;
    }

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
        bool ok = false;
        const double number = valueText.toDouble(&ok);
        if (!ok) {
            continue;
        }

        if (key == QStringLiteral("threshold_absolute") || key == QStringLiteral("absolute")) {
            config.thresholdAbsolute = ImageUtils::normalizeThresholdToMono8(number);
        } else if (key == QStringLiteral("threshold_sigma") || key == QStringLiteral("sigma")) {
            config.sigmaThreshold = std::max(0.0, number);
        } else if (key == QStringLiteral("threshold_peak_fraction") ||
                   key == QStringLiteral("peak_fraction")) {
            config.peakFraction = std::clamp(number, 0.01, 0.95);
        } else if (key == QStringLiteral("threshold_min_intensity") ||
                   key == QStringLiteral("min_intensity")) {
            config.minimumIntensity = ImageUtils::normalizeThresholdToMono8(number);
        } else if (key == QStringLiteral("star_min_area")) {
            config.minArea = std::max(1, static_cast<int>(std::lround(number)));
        } else if (key == QStringLiteral("star_max_area")) {
            config.maxArea = std::max(config.minArea, static_cast<int>(std::lround(number)));
        }
    }
    return config;
}

InitialStarDetectionConfig sanitizeInitialStarDetectionConfig(InitialStarDetectionConfig config)
{
    config.thresholdAbsolute =
        config.thresholdAbsolute >= 0.0 ? ImageUtils::normalizeThresholdToMono8(config.thresholdAbsolute) : -1.0;
    config.sigmaThreshold = std::clamp(config.sigmaThreshold, 0.0, 20.0);
    config.peakFraction = std::clamp(config.peakFraction, 0.01, 0.95);
    config.minimumIntensity = ImageUtils::normalizeThresholdToMono8(std::max(0.0, config.minimumIntensity));
    config.minArea = std::max(1, config.minArea);
    config.maxArea = std::max(config.minArea, config.maxArea);
    return config;
}

InitialStarDetectionConfig& mutableInitialStarDetectionConfig()
{
    static InitialStarDetectionConfig config =
        sanitizeInitialStarDetectionConfig(loadInitialStarDetectionConfig());
    return config;
}

} // namespace

InitialStarDetectionConfig currentInitialStarDetectionConfig()
{
    return mutableInitialStarDetectionConfig();
}

void setCurrentInitialStarDetectionConfig(const InitialStarDetectionConfig& config)
{
    mutableInitialStarDetectionConfig() = sanitizeInitialStarDetectionConfig(config);
}
