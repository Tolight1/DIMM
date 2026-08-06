#pragma once

#include <QString>
#include <QVector>

struct CatalogStar {
    quint64 sourceId = 0;
    QString name;
    double raDeg = 0.0;
    double decDeg = 0.0;
    double catalogEpochYear = 1991.25;
    double properMotionRaMasYr = 0.0;
    double properMotionDecMasYr = 0.0;
    double magnitude = 0.0;
    bool isPolaris = false;
};

class PolarisCatalog {
public:
    static const QVector<CatalogStar>& stars();
    static const CatalogStar* polaris();
    static bool isValid();

private:
    static bool validateCatalog(const QVector<CatalogStar>& stars);
};
