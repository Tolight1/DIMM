#include "ExposureFrameRateRules.h"

#include <cmath>

#include <QStringList>

namespace {

QString compactNumber(double value)
{
    return QString::number(value, 'g', 12);
}

} // namespace

QString formatExposureFrameRateWindows(const QVector<ExposureFrameRateWindow>& windows)
{
    QStringList parts;
    parts.reserve(windows.size());
    for (const ExposureFrameRateWindow& window : windows) {
        parts << QStringLiteral("%1-%2:%3")
                     .arg(compactNumber(window.minExposureMs),
                          compactNumber(window.maxExposureMs),
                          compactNumber(window.frameRateHz));
    }
    return parts.join(QLatin1Char(';'));
}

bool parseExposureFrameRateWindows(const QString& text,
                                   QVector<ExposureFrameRateWindow>* windows,
                                   QString* reason)
{
    if (!windows) {
        if (reason) {
            *reason = QStringLiteral("曝光-帧率区间输出参数无效");
        }
        return false;
    }

    QVector<ExposureFrameRateWindow> parsed;
    const QStringList entries =
        text.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& rawEntry : entries) {
        const QString entry = rawEntry.trimmed();
        const QStringList rateParts = entry.split(QLatin1Char(':'));
        if (rateParts.size() != 2) {
            if (reason) {
                *reason = QStringLiteral("曝光-帧率区间格式应为 1-4:200;4-9:100");
            }
            return false;
        }

        const QStringList exposureParts =
            rateParts[0].trimmed().split(QLatin1Char('-'));
        if (exposureParts.size() != 2) {
            if (reason) {
                *reason = QStringLiteral("曝光区间格式应为 下限-上限，例如 1-4");
            }
            return false;
        }

        bool minOk = false;
        bool maxOk = false;
        bool rateOk = false;
        ExposureFrameRateWindow window;
        window.minExposureMs = exposureParts[0].trimmed().toDouble(&minOk);
        window.maxExposureMs = exposureParts[1].trimmed().toDouble(&maxOk);
        window.frameRateHz = rateParts[1].trimmed().toDouble(&rateOk);
        if (!minOk || !maxOk || !rateOk) {
            if (reason) {
                *reason = QStringLiteral("曝光-帧率区间必须使用数字");
            }
            return false;
        }
        parsed.append(window);
    }

    QString validationReason;
    if (!validateExposureFrameRateWindows(parsed, &validationReason)) {
        if (reason) {
            *reason = validationReason;
        }
        return false;
    }

    *windows = parsed;
    if (reason) {
        reason->clear();
    }
    return true;
}

bool validateExposureFrameRateWindows(const QVector<ExposureFrameRateWindow>& windows,
                                      QString* reason)
{
    if (windows.isEmpty()) {
        if (reason) {
            *reason = QStringLiteral("曝光-帧率区间不能为空");
        }
        return false;
    }

    double previousMaxExposureMs = -1.0;
    for (int i = 0; i < windows.size(); ++i) {
        const ExposureFrameRateWindow& window = windows[i];
        if (!std::isfinite(window.minExposureMs) ||
            !std::isfinite(window.maxExposureMs) ||
            !std::isfinite(window.frameRateHz) ||
            window.minExposureMs <= 0.0 ||
            window.maxExposureMs <= window.minExposureMs ||
            window.frameRateHz <= 0.0) {
            if (reason) {
                *reason = QStringLiteral("曝光-帧率区间必须满足曝光下限>0、曝光上限>下限、帧率>0");
            }
            return false;
        }

        if (previousMaxExposureMs >= 0.0 &&
            window.minExposureMs < previousMaxExposureMs) {
            if (reason) {
                *reason = QStringLiteral("曝光-帧率区间必须按曝光升序排列且不能重叠");
            }
            return false;
        }

        const double theoreticalMaxFrameRateHz = 1000.0 / window.maxExposureMs;
        if (!(window.frameRateHz < theoreticalMaxFrameRateHz)) {
            if (reason) {
                *reason = QStringLiteral("曝光-帧率区间帧率必须严格小于区间最大曝光对应的最大帧率");
            }
            return false;
        }

        previousMaxExposureMs = window.maxExposureMs;
    }

    if (reason) {
        reason->clear();
    }
    return true;
}

std::optional<ExposureFrameRateSelection> selectConfiguredCommonRoiFrameRateForPairUs(
    double exposureAUs,
    double exposureBUs,
    const QVector<ExposureFrameRateWindow>& windows)
{
    const bool validA = std::isfinite(exposureAUs) && exposureAUs > 0.0;
    const bool validB = std::isfinite(exposureBUs) && exposureBUs > 0.0;
    if (!validA && !validB) {
        return std::nullopt;
    }
    const double limitingExposureUs =
        validA && validB ? std::max(exposureAUs, exposureBUs)
                         : (validA ? exposureAUs : exposureBUs);
    const double exposureMs = limitingExposureUs / 1000.0;
    for (int i = 0; i < windows.size(); ++i) {
        const ExposureFrameRateWindow& window = windows[i];
        const bool isLastWindow = i == windows.size() - 1;
        const bool sharesLowerBoundaryWithPrevious =
            i > 0 && window.minExposureMs == windows[i - 1].maxExposureMs;
        const bool sharesUpperBoundaryWithNext =
            !isLastWindow && window.maxExposureMs == windows[i + 1].minExposureMs;
        const bool inWindow =
            (sharesLowerBoundaryWithPrevious
                 ? exposureMs > window.minExposureMs
                 : exposureMs >= window.minExposureMs) &&
            (exposureMs < window.maxExposureMs ||
             ((isLastWindow || sharesUpperBoundaryWithNext) &&
              exposureMs <= window.maxExposureMs));
        if (inWindow) {
            return ExposureFrameRateSelection{
                window.frameRateHz, true, false, QStringLiteral("exposure_window")};
        }
    }
    return std::nullopt;
}
