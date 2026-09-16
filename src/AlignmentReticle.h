#pragma once

#include <QVector>
#include <QString>

namespace AlignmentReticle {

enum class TickKind {
    Subdivision,
    Major
};

struct Tick {
    double hour = 0.0;
    // phaseRad is zero at the top of the reticle and increases clockwise.
    double phaseRad = 0.0;
    TickKind kind = TickKind::Subdivision;
};

QVector<Tick> buildTicks();
QString formatHourLabel(int hour);

}
