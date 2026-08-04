#pragma once

#include "AppConfig.h"

#include <QDateTime>
#include <QString>

struct AutoAcquisitionWindow {
    bool valid = false;
    QDateTime start;
    QDateTime stop;
    QString windowId;
    QString errorMessage;
};

class AutoAcquisitionScheduler {
public:
    static AutoAcquisitionWindow resolveWindow(const AutoAcquisitionConfig& config,
                                               const QDateTime& now);
    static bool contains(const AutoAcquisitionWindow& window,
                         const QDateTime& now);
    static QString formatWindowPreview(const AutoAcquisitionWindow& window);

private:
    static AutoAcquisitionWindow resolveTestWindow(const AutoAcquisitionConfig& config,
                                                   const QDateTime& now);
    static AutoAcquisitionWindow resolveSunWindow(const AutoAcquisitionConfig& config,
                                                  const QDateTime& now);
};
