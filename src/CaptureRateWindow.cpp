#include "CaptureRateWindow.h"

namespace {
constexpr qint64 kCaptureRateWindowMs = 1000;
}

void CaptureRateWindow::clear()
{
    m_capturedAtMs.clear();
}

void CaptureRateWindow::record(qint64 capturedAtMs)
{
    m_capturedAtMs.append(capturedAtMs);
    const qint64 oldestAllowedMs = capturedAtMs - kCaptureRateWindowMs;
    while (!m_capturedAtMs.isEmpty() && m_capturedAtMs.front() < oldestAllowedMs) {
        m_capturedAtMs.removeFirst();
    }
}

double CaptureRateWindow::rateHzAt(qint64 nowMs) const
{
    const qint64 oldestAllowedMs = nowMs - kCaptureRateWindowMs;
    int frameCount = 0;
    for (const qint64 capturedAtMs : m_capturedAtMs) {
        if (capturedAtMs >= oldestAllowedMs && capturedAtMs <= nowMs) {
            ++frameCount;
        }
    }
    return static_cast<double>(frameCount) * 1000.0 /
           static_cast<double>(kCaptureRateWindowMs);
}
