#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

struct TriangleInvariant {
    bool valid = false;
    double r1 = 0.0;
    double r2 = 0.0;
    double longestDistance = 0.0;
    double signedArea = 0.0;
};

struct SimilarityTransform2D {
    double scale = 1.0;
    double rotationRad = 0.0;
    QPointF translation;
    bool mirrored = false;
};

struct PatternMatcherConfig {
    int minMatchedStars = 5;
    double initialMaxResidualPx = 8.0;
    double maxResidualPx = 4.0;
    double ratioTolerance = 0.03;
    double minTriangleLongestPx = 20.0;
    double minPlateScaleArcsecPx = 1.70;
    double maxPlateScaleArcsecPx = 2.15;
    double nominalPlateScaleArcsecPx = 1.9179;
    int maxCatalogTrianglesPerImageTriangle = 256;
    int maxTestedTransforms = 10000;
    double minScoreMargin = 5.0;
    double minMatchedSpatialSpreadPx = 50.0;
    std::shared_ptr<std::atomic_bool> cancelled;
};

struct PatternCatalogPoint {
    quint64 sourceId = 0;
    QString name;
    QPointF planeRad;
    double magnitude = 0.0;
    bool isPolaris = false;
};

struct PatternMatchPair {
    int detectedIndex = -1;
    int catalogIndex = -1;
    QPointF detectedPixel;
    QPointF predictedPixel;
    double residualPx = 0.0;
};

struct PatternMatchResult {
    bool valid = false;
    int matchedCount = 0;
    int catalogTriangleCount = 0;
    int imageTriangleCount = 0;
    int candidateTriangleCount = 0;
    int testedTransformCount = 0;
    int solutionClusterCount = 0;
    int bestSolutionSupportCount = 0;
    int secondBestSolutionSupportCount = 0;
    double rmsPx = 0.0;
    double maxResidualPx = 0.0;
    double matchedSpatialSpreadPx = 0.0;
    bool hasNorthCelestialPolePixel = false;
    QPointF northCelestialPolePixel;
    bool hasPolarisPixel = false;
    QPointF polarisPixel;
    double polarisPolarRadiusPx = 0.0;
    double bestScore = 0.0;
    double secondBestScore = 0.0;
    double scoreMargin = 0.0;
    SimilarityTransform2D transform;
    QVector<PatternMatchPair> pairs;
};

TriangleInvariant makeTriangleInvariant(const QPointF& a,
                                         const QPointF& b,
                                         const QPointF& c);
QPointF applySimilarityTransform(const SimilarityTransform2D& transform,
                                 const QPointF& point);
SimilarityTransform2D makeSimilarityTransformFromPairs(const QPointF& sourceA,
                                                       const QPointF& sourceB,
                                                       const QPointF& targetA,
                                                       const QPointF& targetB,
                                                       bool mirrored);

class StarPatternMatcher {
public:
    PatternMatchResult matchDetectedToCatalog(
        const QVector<QPointF>& detectedPoints,
        const QVector<PatternCatalogPoint>& catalogPoints,
        const PatternMatcherConfig& config) const;

    PatternMatchResult matchPoints(const QVector<QPointF>& detectedPoints,
                                   const QVector<PatternCatalogPoint>& catalogPoints,
                                   const SimilarityTransform2D& initialTransform,
                                   double initialMaxResidualPx,
                                   double refinedMaxResidualPx,
                                   int minMatchedStars = 5) const;

private:
    QVector<PatternMatchPair> collectNearestOneToOneMatches(
        const QVector<QPointF>& detectedPoints,
        const QVector<PatternCatalogPoint>& catalogPoints,
        const SimilarityTransform2D& transform,
        double maxResidualPx) const;
    SimilarityTransform2D fitSimilarityTransform(
        const QVector<PatternCatalogPoint>& catalogPoints,
        const QVector<PatternMatchPair>& pairs,
        const SimilarityTransform2D& initialTransform) const;
    static void annotateSolvedReferencePoints(
        const QVector<PatternCatalogPoint>& catalogPoints,
        PatternMatchResult* result);
    static double computeRmsPx(const QVector<PatternMatchPair>& pairs);
    static double computeMaxResidualPx(const QVector<PatternMatchPair>& pairs);
    static double computeMatchedSpatialSpreadPx(const QVector<PatternMatchPair>& pairs);
};
