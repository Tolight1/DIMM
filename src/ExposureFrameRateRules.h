#pragma once

#include <QVector>
#include <QString>

#include <optional>

struct ExposureFrameRateWindow {
    double minExposureMs = 0.0;
    double maxExposureMs = 0.0;
    double frameRateHz = 0.0;
};

struct ExposureFrameRateSelection {
    double frameRateHz = 0.0;
    bool selectedFromWindow = false;
    bool clampedByExposureSafety = false;
    QString reason;
};

QString formatExposureFrameRateWindows(const QVector<ExposureFrameRateWindow>& windows);

bool parseExposureFrameRateWindows(const QString& text,
                                   QVector<ExposureFrameRateWindow>* windows,
                                   QString* reason = nullptr);

bool validateExposureFrameRateWindows(const QVector<ExposureFrameRateWindow>& windows,
                                      QString* reason = nullptr);

std::optional<ExposureFrameRateSelection> selectConfiguredCommonRoiFrameRateForPairUs(
    double exposureAUs,
    double exposureBUs,
    const QVector<ExposureFrameRateWindow>& windows);
