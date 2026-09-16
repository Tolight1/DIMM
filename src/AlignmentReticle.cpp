#include "AlignmentReticle.h"

namespace AlignmentReticle {
namespace {
constexpr double kTwoPi = 6.28318530717958647692;
constexpr int kTicksPerHour = 6;
constexpr int kClockHourCount = 12;
constexpr int kTickCount = kTicksPerHour * kClockHourCount;
}

QVector<Tick> buildTicks()
{
    QVector<Tick> ticks;
    ticks.reserve(kTickCount);

    for (int index = 0; index < kTickCount; ++index) {
        Tick tick;
        tick.hour = static_cast<double>(index) / static_cast<double>(kTicksPerHour);
        tick.phaseRad = static_cast<double>(index) / static_cast<double>(kTickCount) * kTwoPi;
        tick.kind = index % kTicksPerHour == 0 ? TickKind::Major : TickKind::Subdivision;
        ticks.append(tick);
    }

    return ticks;
}

QString formatHourLabel(int hour)
{
    const int normalizedHour = ((hour % kClockHourCount) + kClockHourCount) % kClockHourCount;
    if (normalizedHour == 0) {
        return QStringLiteral("12");
    }
    return QString::number(normalizedHour);
}

}
