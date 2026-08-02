#include "AstronomyTransform.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

CatalogSkyPosition propagateProperMotion(const CatalogStar& star, double targetEpochYear)
{
    const double years = targetEpochYear - star.catalogEpochYear;
    const double decDeg = star.decDeg + (star.properMotionDecMasYr * years / 3600000.0);
    const double cosDec = std::max(1e-6, std::abs(std::cos(qDegreesToRadians(star.decDeg))));
    double raDeg = star.raDeg + (star.properMotionRaMasYr * years / 3600000.0) / cosDec;
    raDeg = std::fmod(raDeg, 360.0);
    if (raDeg < 0.0) {
        raDeg += 360.0;
    }
    return CatalogSkyPosition{raDeg, std::clamp(decDeg, -90.0, 90.0), targetEpochYear};
}

QVector3D vectorFromRaDecDeg(double raDeg, double decDeg)
{
    const double raRad = qDegreesToRadians(raDeg);
    const double decRad = qDegreesToRadians(decDeg);
    const double cosDec = std::cos(decRad);
    return QVector3D(static_cast<float>(cosDec * std::cos(raRad)),
                     static_cast<float>(cosDec * std::sin(raRad)),
                     static_cast<float>(std::sin(decRad))).normalized();
}

QVector3D catalogVectorAtEpoch(const CatalogStar& star, double targetEpochYear)
{
    const CatalogSkyPosition position = propagateProperMotion(star, targetEpochYear);
    return vectorFromRaDecDeg(position.raDeg, position.decDeg);
}

QPointF northPolePlaneFromRaDecDeg(double raDeg, double decDeg)
{
    const double raRad = qDegreesToRadians(raDeg);
    const double decRad = qDegreesToRadians(decDeg);
    const double rho = qDegreesToRadians(90.0) - decRad;
    return QPointF(rho * std::cos(raRad), rho * std::sin(raRad));
}

QPointF northPolePlaneAtEpoch(const CatalogStar& star, double targetEpochYear)
{
    const CatalogSkyPosition position = propagateProperMotion(star, targetEpochYear);
    return northPolePlaneFromRaDecDeg(position.raDeg, position.decDeg);
}

double angularDistanceDeg(const CatalogStar& a, const CatalogStar& b)
{
    const double targetEpochYear = std::max(a.catalogEpochYear, b.catalogEpochYear);
    const QVector3D av = catalogVectorAtEpoch(a, targetEpochYear);
    const QVector3D bv = catalogVectorAtEpoch(b, targetEpochYear);
    const double dot = std::clamp(static_cast<double>(QVector3D::dotProduct(av, bv)), -1.0, 1.0);
    return qRadiansToDegrees(std::acos(dot));
}
