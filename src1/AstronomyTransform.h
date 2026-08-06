#pragma once

#include "PolarisCatalog.h"

#include <QPointF>
#include <QVector3D>

struct CatalogSkyPosition {
    double raDeg = 0.0;
    double decDeg = 0.0;
    double epochYear = 1991.25;
};

CatalogSkyPosition propagateProperMotion(const CatalogStar& star, double targetEpochYear);
QVector3D vectorFromRaDecDeg(double raDeg, double decDeg);
QVector3D catalogVectorAtEpoch(const CatalogStar& star, double targetEpochYear);
QPointF northPolePlaneFromRaDecDeg(double raDeg, double decDeg);
QPointF northPolePlaneAtEpoch(const CatalogStar& star, double targetEpochYear);
double angularDistanceDeg(const CatalogStar& a, const CatalogStar& b);
