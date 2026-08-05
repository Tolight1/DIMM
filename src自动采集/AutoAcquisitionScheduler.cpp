#include "AutoAcquisitionScheduler.h"

#include <QtMath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double degToRad(double deg)
{
    return deg * kPi / 180.0;
}

double radToDeg(double rad)
{
    return rad * 180.0 / kPi;
}

double normalizeDegrees(double deg)
{
    double value = std::fmod(deg, 360.0);
    if (value < 0.0) {
        value += 360.0;
    }
    return value;
}

QDateTime localDateTimeForTime(const QDate& date, const QTime& time)
{
    return QDateTime(date, time, Qt::LocalTime);
}

QString windowIdForRange(const QDateTime& start, const QDateTime& stop)
{
    return QStringLiteral("%1/%2")
        .arg(start.toMSecsSinceEpoch())
        .arg(stop.toMSecsSinceEpoch());
}

bool calculateSunEvent(const QDate& date,
                       double latitudeDeg,
                       double longitudeDeg,
                       bool sunrise,
                       QDateTime* eventTime,
                       QString* errorMessage)
{
    const double zenithDeg = 90.833;
    const int dayOfYear = date.dayOfYear();
    const double gamma =
        2.0 * kPi / 365.0 * (static_cast<double>(dayOfYear) - 1.0 + (sunrise ? 6.0 : 18.0) / 24.0);
    const double equationOfTime =
        229.18 * (0.000075 + 0.001868 * std::cos(gamma) - 0.032077 * std::sin(gamma) -
                  0.014615 * std::cos(2.0 * gamma) - 0.040849 * std::sin(2.0 * gamma));
    const double solarDeclination =
        0.006918 - 0.399912 * std::cos(gamma) + 0.070257 * std::sin(gamma) -
        0.006758 * std::cos(2.0 * gamma) + 0.000907 * std::sin(2.0 * gamma) -
        0.002697 * std::cos(3.0 * gamma) + 0.00148 * std::sin(3.0 * gamma);

    const double latitudeRad = degToRad(latitudeDeg);
    const double cosHourAngle =
        (std::cos(degToRad(zenithDeg)) / (std::cos(latitudeRad) * std::cos(solarDeclination))) -
        (std::tan(latitudeRad) * std::tan(solarDeclination));

    if (cosHourAngle > 1.0) {
        if (errorMessage) {
            *errorMessage = sunrise ? QStringLiteral("No local sunrise for this date")
                                    : QStringLiteral("No local sunset for this date");
        }
        return false;
    }
    if (cosHourAngle < -1.0) {
        if (errorMessage) {
            *errorMessage = sunrise ? QStringLiteral("No local sunrise for this date")
                                    : QStringLiteral("No local sunset for this date");
        }
        return false;
    }

    const double hourAngle =
        sunrise ? radToDeg(std::acos(cosHourAngle)) : -radToDeg(std::acos(cosHourAngle));
    const QDateTime localNoon(date, QTime(12, 0), Qt::LocalTime);
    const int timezoneMinutes = localNoon.offsetFromUtc() / 60;
    const double solarMinutes =
        720.0 - 4.0 * (longitudeDeg + hourAngle) - equationOfTime + timezoneMinutes;
    const int secondsFromMidnight = qRound(solarMinutes * 60.0);
    *eventTime = QDateTime(date, QTime(0, 0), Qt::LocalTime).addSecs(secondsFromMidnight);
    return true;
}

} // namespace

AutoAcquisitionWindow AutoAcquisitionScheduler::resolveWindow(const AutoAcquisitionConfig& config,
                                                              const QDateTime& now)
{
    if (config.testTimeOverrideEnabled) {
        return resolveTestWindow(config, now);
    }
    return resolveSunWindow(config, now);
}

bool AutoAcquisitionScheduler::contains(const AutoAcquisitionWindow& window,
                                        const QDateTime& now)
{
    return window.valid && now >= window.start && now < window.stop;
}

QString AutoAcquisitionScheduler::formatWindowPreview(const AutoAcquisitionWindow& window)
{
    if (!window.valid) {
        return window.errorMessage;
    }
    return QStringLiteral("%1 -> %2")
        .arg(window.start.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
             window.stop.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
}

AutoAcquisitionWindow AutoAcquisitionScheduler::resolveTestWindow(const AutoAcquisitionConfig& config,
                                                                  const QDateTime& now)
{
    AutoAcquisitionWindow window;
    QDateTime start = localDateTimeForTime(now.date(), config.testStartTime);
    QDateTime stop = localDateTimeForTime(now.date(), config.testStopTime);
    if (stop <= start) {
        stop = stop.addDays(1);
        if (now < start) {
            start = start.addDays(-1);
            stop = stop.addDays(-1);
        }
    }
    window.valid = config.testStartTime.isValid() && config.testStopTime.isValid();
    window.start = start;
    window.stop = stop;
    window.windowId = windowIdForRange(start, stop);
    if (!window.valid) {
        window.errorMessage = QStringLiteral("Invalid test auto-acquisition time");
    }
    return window;
}

AutoAcquisitionWindow AutoAcquisitionScheduler::resolveSunWindow(const AutoAcquisitionConfig& config,
                                                                 const QDateTime& now)
{
    AutoAcquisitionWindow window;
    QDate activeDate = now.date();
    QDateTime todaySunrise;
    QString todaySunriseError;
    if (calculateSunEvent(now.date(),
                          config.latitudeDeg,
                          config.longitudeDeg,
                          true,
                          &todaySunrise,
                          &todaySunriseError) &&
        now < todaySunrise) {
        activeDate = now.date().addDays(-1);
    }

    QDateTime sunset;
    QString sunsetError;
    if (!calculateSunEvent(activeDate,
                           config.latitudeDeg,
                           config.longitudeDeg,
                           false,
                           &sunset,
                           &sunsetError)) {
        window.errorMessage = sunsetError;
        return window;
    }

    QDateTime sunrise;
    QString sunriseError;
    if (!calculateSunEvent(activeDate.addDays(1),
                           config.latitudeDeg,
                           config.longitudeDeg,
                           true,
                           &sunrise,
                           &sunriseError)) {
        window.errorMessage = sunriseError;
        return window;
    }

    window.start = sunset.addSecs(config.startOffsetMinutesAfterSunset * 60);
    window.stop = sunrise.addSecs(-config.stopOffsetMinutesBeforeSunrise * 60);
    if (window.stop <= window.start) {
        window.errorMessage = QStringLiteral("Auto-acquisition stop time is not after start time");
        return window;
    }
    window.valid = true;
    window.windowId = windowIdForRange(window.start, window.stop);
    return window;
}
