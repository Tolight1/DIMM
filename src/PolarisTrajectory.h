#pragma once

#include <QPointF>
#include <QVector>

namespace PolarisTrajectory {

inline constexpr double kSiderealDaySeconds = 86164.0905;

struct RoiTrackFit {
    bool valid = false;
    QPointF center;
    double radiusPx = 0.0;
    double rmsPx = 0.0;
};

double normalizePhase(double phaseRad);
double advancedPhase(double phaseRad, double elapsedSeconds);
QPointF clockwisePoint(const QPointF& center, double radiusPx, double phaseRad);
double phaseFromClockwisePoint(const QPointF& center, const QPointF& point);
QPointF deriveCenterFromPhase(const QPointF& confirmedPoint,
                              double radiusPx,
                              double phaseRad);

class RoiTrajectoryAccumulator {
public:
    void clear();
    void append(const QPointF& point);
    QVector<QPointF> points() const { return m_points; }
    RoiTrackFit fitCircle() const;

private:
    QVector<QPointF> m_points;
};

}
