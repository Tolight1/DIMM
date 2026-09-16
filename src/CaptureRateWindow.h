#pragma once

#include <QVector>
#include <QtGlobal>

class CaptureRateWindow
{
public:
    void clear();
    void record(qint64 capturedAtMs);
    double rateHzAt(qint64 nowMs) const;

private:
    QVector<qint64> m_capturedAtMs;
};
