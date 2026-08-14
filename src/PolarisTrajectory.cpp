#include "PolarisTrajectory.h"

#include <cmath>
#include <limits>

namespace PolarisTrajectory {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

double det3(double a00,
            double a01,
            double a02,
            double a10,
            double a11,
            double a12,
            double a20,
            double a21,
            double a22)
{
    return a00 * (a11 * a22 - a12 * a21) -
           a01 * (a10 * a22 - a12 * a20) +
           a02 * (a10 * a21 - a11 * a20);
}

}

double normalizePhase(double phaseRad)
{
    if (!std::isfinite(phaseRad)) {
        return 0.0;
    }

    double normalized = std::fmod(phaseRad, kTwoPi);
    if (normalized < 0.0) {
        normalized += kTwoPi;
    }
    return normalized;
}

double advancedPhase(double phaseRad, double elapsedSeconds)
{
    if (!std::isfinite(elapsedSeconds)) {
        return normalizePhase(phaseRad);
    }

    return normalizePhase(phaseRad + elapsedSeconds / kSiderealDaySeconds * kTwoPi);
}

QPointF clockwisePoint(const QPointF& center, double radiusPx, double phaseRad)
{
    if (!std::isfinite(radiusPx) || radiusPx <= 0.0) {
        return center;
    }

    const double phase = normalizePhase(phaseRad);
    return QPointF(center.x() + radiusPx * std::sin(phase),
                   center.y() - radiusPx * std::cos(phase));
}

double phaseFromClockwisePoint(const QPointF& center, const QPointF& point)
{
    return normalizePhase(std::atan2(point.x() - center.x(),
                                     center.y() - point.y()));
}

QPointF deriveCenterFromPhase(const QPointF& confirmedPoint,
                              double radiusPx,
                              double phaseRad)
{
    if (!std::isfinite(radiusPx) || radiusPx <= 0.0) {
        return confirmedPoint;
    }

    const double phase = normalizePhase(phaseRad);
    const QPointF offset(radiusPx * std::sin(phase),
                         -radiusPx * std::cos(phase));
    return confirmedPoint - offset;
}

void RoiTrajectoryAccumulator::clear()
{
    m_points.clear();
}

void RoiTrajectoryAccumulator::append(const QPointF& point)
{
    if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
        return;
    }
    m_points.append(point);
}

RoiTrackFit RoiTrajectoryAccumulator::fitCircle() const
{
    RoiTrackFit fit;
    if (m_points.size() < 3) {
        return fit;
    }

    double sumX = 0.0;
    double sumY = 0.0;
    double sumX2 = 0.0;
    double sumY2 = 0.0;
    double sumXY = 0.0;
    double sumR2 = 0.0;
    double sumXR2 = 0.0;
    double sumYR2 = 0.0;

    for (const QPointF& point : m_points) {
        const double x = point.x();
        const double y = point.y();
        const double r2 = x * x + y * y;
        sumX += x;
        sumY += y;
        sumX2 += x * x;
        sumY2 += y * y;
        sumXY += x * y;
        sumR2 += r2;
        sumXR2 += x * r2;
        sumYR2 += y * r2;
    }

    const double n = static_cast<double>(m_points.size());
    const double determinant = det3(sumX2, sumXY, sumX,
                                    sumXY, sumY2, sumY,
                                    sumX, sumY, n);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-9) {
        return fit;
    }

    const double rhs0 = -sumXR2;
    const double rhs1 = -sumYR2;
    const double rhs2 = -sumR2;

    const double a = det3(rhs0, sumXY, sumX,
                          rhs1, sumY2, sumY,
                          rhs2, sumY, n) / determinant;
    const double b = det3(sumX2, rhs0, sumX,
                          sumXY, rhs1, sumY,
                          sumX, rhs2, n) / determinant;
    const double c = det3(sumX2, sumXY, rhs0,
                          sumXY, sumY2, rhs1,
                          sumX, sumY, rhs2) / determinant;

    const QPointF center(-0.5 * a, -0.5 * b);
    const double radiusSquared = center.x() * center.x() +
                                 center.y() * center.y() -
                                 c;
    if (!std::isfinite(radiusSquared) || radiusSquared <= 0.0) {
        return fit;
    }

    const double radius = std::sqrt(radiusSquared);
    if (!std::isfinite(radius) || radius <= 0.0) {
        return fit;
    }

    double squaredError = 0.0;
    for (const QPointF& point : m_points) {
        const double distance = std::hypot(point.x() - center.x(),
                                           point.y() - center.y());
        const double residual = distance - radius;
        squaredError += residual * residual;
    }

    fit.valid = true;
    fit.center = center;
    fit.radiusPx = radius;
    fit.rmsPx = std::sqrt(squaredError / n);
    return fit;
}

}
