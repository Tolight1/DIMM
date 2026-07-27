#include "PolarisCatalog.h"

#include "PolarisCatalogData.h"

#include <cmath>
#include <iterator>

const QVector<CatalogStar>& PolarisCatalog::stars()
{
    static const QVector<CatalogStar> catalog(
        std::begin(kPolarisCatalogStars),
        std::end(kPolarisCatalogStars));
    return catalog;
}

const CatalogStar* PolarisCatalog::polaris()
{
    const QVector<CatalogStar>& catalog = stars();
    for (const CatalogStar& star : catalog) {
        if (star.isPolaris) {
            return &star;
        }
    }
    return nullptr;
}

bool PolarisCatalog::isValid()
{
    return validateCatalog(stars());
}

bool PolarisCatalog::validateCatalog(const QVector<CatalogStar>& stars)
{
    int polarisCount = 0;
    for (const CatalogStar& star : stars) {
        if (star.sourceId == 0 ||
            !std::isfinite(star.raDeg) ||
            !std::isfinite(star.decDeg) ||
            !std::isfinite(star.catalogEpochYear) ||
            !std::isfinite(star.properMotionRaMasYr) ||
            !std::isfinite(star.properMotionDecMasYr) ||
            !std::isfinite(star.magnitude) ||
            star.decDeg < 86.0 ||
            star.decDeg > 90.0 ||
            star.magnitude > 9.5) {
            return false;
        }
        if (star.isPolaris) {
            ++polarisCount;
        }
    }
    return polarisCount == 1;
}
