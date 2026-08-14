#include "DimmRuntimeHelpers.h"

#include "CameraManager.h"

#include <algorithm>

#include <QTime>

double medianOfSamples(QVector<double> samples)
{
    if (samples.isEmpty()) {
        return 0.0;
    }

    std::sort(samples.begin(), samples.end());
    const int middle = samples.size() / 2;
    if ((samples.size() % 2) == 1) {
        return samples[middle];
    }
    return (samples[middle - 1] + samples[middle]) * 0.5;
}

double decimalYearFromUtc(const QDateTime& utcDateTime)
{
    const QDateTime utc = utcDateTime.toUTC();
    const QDate date = utc.date();
    const QTime time = utc.time();
    const int year = date.year();
    const QDate startDate(year, 1, 1);
    const QDate nextYearDate(year + 1, 1, 1);
    const double dayOffset = static_cast<double>(startDate.daysTo(date));
    const double secondsOfDay =
        static_cast<double>(QTime(0, 0).secsTo(time)) + static_cast<double>(time.msec()) / 1000.0;
    const double daysInYear = static_cast<double>(startDate.daysTo(nextYearDate));
    return static_cast<double>(year) + (dayOffset + secondsOfDay / 86400.0) / daysInYear;
}

qint64 safeRoiIncrement(qint64 increment)
{
    return increment > 0 ? increment : 1;
}

qint64 alignRoiValue(qint64 value, const RoiAxisRange& range)
{
    const qint64 increment = safeRoiIncrement(range.increment);
    const qint64 clamped = std::clamp(value, range.minValue, range.maxValue);
    const qint64 steps = (clamped - range.minValue) / increment;
    const qint64 aligned = range.minValue + steps * increment;
    return std::clamp(aligned, range.minValue, range.maxValue);
}


QString toggleButtonStyle(bool active)
{
    return active
               ? QStringLiteral("background-color: #20496b; border: 1px solid #56d4ff; color: #f8fcff;")
               : QString();
}

QString uiStatusColor(UiStatusLevel level)
{
    switch (level) {
    case UiStatusLevel::Info:
        return QStringLiteral("#56d4ff");
    case UiStatusLevel::Success:
        return QStringLiteral("#95dd6b");
    case UiStatusLevel::Warning:
        return QStringLiteral("#ffbe55");
    case UiStatusLevel::Error:
        return QStringLiteral("#ff5c57");
    case UiStatusLevel::Muted:
    default:
        return QStringLiteral("#8ea5bb");
    }
}

QString cameraStatusText(bool online)
{
    return online ? QStringLiteral("在线") : QStringLiteral("离线");
}

UiStatusLevel cameraStatusLevel(bool online)
{
    return online ? UiStatusLevel::Success : UiStatusLevel::Muted;
}

QString statusLabelStyle(const QString& color)
{
    return QStringLiteral("color: %1; background: transparent; padding: 0 12px 8px 12px;").arg(color);
}

QString statusLabelStyle(UiStatusLevel level)
{
    return statusLabelStyle(uiStatusColor(level));
}

bool pulseConfigsMatch(const PulseGeneratorManager::Config& lhs,
                       const PulseGeneratorManager::Config& rhs)
{
    return lhs.enabled == rhs.enabled &&
           lhs.portName.trimmed().compare(rhs.portName.trimmed(), Qt::CaseInsensitive) == 0 &&
           lhs.baudRate == rhs.baudRate &&
           lhs.terminalId == rhs.terminalId &&
           qFuzzyCompare(lhs.frequencyHz + 1.0, rhs.frequencyHz + 1.0) &&
           lhs.pulseCount == rhs.pulseCount &&
           qFuzzyCompare(lhs.dutyPercent + 1.0, rhs.dutyPercent + 1.0) &&
           lhs.remoteControl == rhs.remoteControl;
}
