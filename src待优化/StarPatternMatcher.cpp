#include "StarPatternMatcher.h"

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>

#include <QHash>

namespace {
struct TriangleIndex {
    int a = 0;
    int b = 0;
    int c = 0;
};

struct TriangleKey {
    int r1Bin = 0;
    int r2Bin = 0;

    bool operator==(const TriangleKey& other) const
    {
        return r1Bin == other.r1Bin && r2Bin == other.r2Bin;
    }
};

struct CatalogTriangleRecord {
    int a = -1;
    int b = -1;
    int c = -1;
    TriangleInvariant invariant;
};

struct MatchCandidateEdge {
    int catalogIndex = -1;
    int detectedIndex = -1;
    QPointF predictedPixel;
    double residualPx = 0.0;
};

struct SolutionSignature {
    QVector<quint64> matchedCatalogSourceIds;
    QVector<int> matchedDetectionIndexes;
    bool mirrored = false;
    int quantizedNcpX = 0;
    int quantizedNcpY = 0;
    int quantizedPolarisX = 0;
    int quantizedPolarisY = 0;
    int quantizedScale = 0;
    int quantizedRotation = 0;
};

struct SolutionCluster {
    PatternMatchResult bestResult;
    SolutionSignature signature;
    double bestScore = -std::numeric_limits<double>::infinity();
    int supportCount = 0;
};

size_t qHash(const TriangleKey& key, size_t seed = 0) noexcept
{
    size_t hash = seed;
    hash ^= static_cast<size_t>(key.r1Bin) + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    hash ^= static_cast<size_t>(key.r2Bin) + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    return hash;
}

double distanceBetween(const QPointF& a, const QPointF& b)
{
    const double dx = a.x() - b.x();
    const double dy = a.y() - b.y();
    return std::hypot(dx, dy);
}

double triangleSignedArea(const QPointF& a, const QPointF& b, const QPointF& c)
{
    return 0.5 * ((b.x() - a.x()) * (c.y() - a.y()) -
                  (b.y() - a.y()) * (c.x() - a.x()));
}

void forEachTriangle(int count, const std::function<void(TriangleIndex)>& callback)
{
    for (int a = 0; a < count - 2; ++a) {
        for (int b = a + 1; b < count - 1; ++b) {
            for (int c = b + 1; c < count; ++c) {
                callback(TriangleIndex{a, b, c});
            }
        }
    }
}

double estimatePlateScaleArcsecPx(double catalogLongestRad, double imageLongestPx)
{
    if (imageLongestPx <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return qRadiansToDegrees(catalogLongestRad) * 3600.0 / imageLongestPx;
}

double scoreResult(const PatternMatchResult& result)
{
    if (result.matchedCount <= 0 || !std::isfinite(result.rmsPx)) {
        return -std::numeric_limits<double>::infinity();
    }
    return result.matchedCount * 100.0 - result.rmsPx * 10.0;
}

double rotationDifferenceDeg(double aRad, double bRad)
{
    double diffDeg = qRadiansToDegrees(aRad - bRad);
    while (diffDeg > 180.0) {
        diffDeg -= 360.0;
    }
    while (diffDeg < -180.0) {
        diffDeg += 360.0;
    }
    return std::abs(diffDeg);
}

SolutionSignature makeSolutionSignature(const PatternMatchResult& result,
                                        const QVector<PatternCatalogPoint>& catalogPoints)
{
    SolutionSignature signature;
    signature.mirrored = result.transform.mirrored;
    for (const PatternMatchPair& pair : result.pairs) {
        if (pair.catalogIndex >= 0 && pair.catalogIndex < catalogPoints.size()) {
            signature.matchedCatalogSourceIds.push_back(catalogPoints[pair.catalogIndex].sourceId);
        }
        signature.matchedDetectionIndexes.push_back(pair.detectedIndex);
    }
    std::sort(signature.matchedCatalogSourceIds.begin(), signature.matchedCatalogSourceIds.end());
    std::sort(signature.matchedDetectionIndexes.begin(), signature.matchedDetectionIndexes.end());
    if (result.hasNorthCelestialPolePixel) {
        signature.quantizedNcpX = static_cast<int>(std::lround(result.northCelestialPolePixel.x() / 5.0));
        signature.quantizedNcpY = static_cast<int>(std::lround(result.northCelestialPolePixel.y() / 5.0));
    }
    if (result.hasPolarisPixel) {
        signature.quantizedPolarisX = static_cast<int>(std::lround(result.polarisPixel.x() / 5.0));
        signature.quantizedPolarisY = static_cast<int>(std::lround(result.polarisPixel.y() / 5.0));
    }
    signature.quantizedScale = static_cast<int>(std::lround(result.transform.scale / 0.002));
    signature.quantizedRotation = static_cast<int>(std::lround(qRadiansToDegrees(result.transform.rotationRad) / 0.2));
    return signature;
}

double setOverlapRatio(const QVector<quint64>& a, const QVector<quint64>& b)
{
    if (a.isEmpty() || b.isEmpty()) {
        return 0.0;
    }
    int i = 0;
    int j = 0;
    int overlap = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) {
            ++overlap;
            ++i;
            ++j;
        } else if (a[i] < b[j]) {
            ++i;
        } else {
            ++j;
        }
    }
    return static_cast<double>(overlap) / static_cast<double>(std::max(a.size(), b.size()));
}

bool samePhysicalSolution(const PatternMatchResult& a, const PatternMatchResult& b)
{
    if (a.transform.mirrored != b.transform.mirrored) {
        return false;
    }
    if (!a.hasNorthCelestialPolePixel || !b.hasNorthCelestialPolePixel ||
        distanceBetween(a.northCelestialPolePixel, b.northCelestialPolePixel) >= 5.0) {
        return false;
    }
    if (a.hasPolarisPixel != b.hasPolarisPixel) {
        return false;
    }
    if (a.hasPolarisPixel &&
        distanceBetween(a.polarisPixel, b.polarisPixel) >= 5.0) {
        return false;
    }
    const double scaleDenominator = std::max({std::abs(a.transform.scale),
                                              std::abs(b.transform.scale),
                                              1e-9});
    const double scaleRelativeDifference =
        std::abs(a.transform.scale - b.transform.scale) / scaleDenominator;
    if (scaleRelativeDifference >= 0.002) {
        return false;
    }
    if (rotationDifferenceDeg(a.transform.rotationRad, b.transform.rotationRad) >= 0.2) {
        return false;
    }
    return true;
}

bool sameSolutionSignature(const SolutionSignature& a, const SolutionSignature& b)
{
    return a.matchedCatalogSourceIds == b.matchedCatalogSourceIds &&
           a.matchedDetectionIndexes == b.matchedDetectionIndexes &&
           a.mirrored == b.mirrored &&
           a.quantizedNcpX == b.quantizedNcpX &&
           a.quantizedNcpY == b.quantizedNcpY &&
           a.quantizedPolarisX == b.quantizedPolarisX &&
           a.quantizedPolarisY == b.quantizedPolarisY &&
           a.quantizedScale == b.quantizedScale &&
           a.quantizedRotation == b.quantizedRotation;
}

void addResultToSolutionClusters(QVector<SolutionCluster>* solutionClusters,
                                 const PatternMatchResult& result,
                                 const QVector<PatternCatalogPoint>& catalogPoints)
{
    if (!solutionClusters || !result.valid) {
        return;
    }
    const double currentScore = scoreResult(result);
    if (!std::isfinite(currentScore)) {
        return;
    }
    const SolutionSignature signature = makeSolutionSignature(result, catalogPoints);
    for (SolutionCluster& cluster : *solutionClusters) {
        const double overlapRatio =
            setOverlapRatio(signature.matchedCatalogSourceIds,
                            cluster.signature.matchedCatalogSourceIds);
        if ((sameSolutionSignature(signature, cluster.signature) ||
             (samePhysicalSolution(result, cluster.bestResult) && overlapRatio >= 0.80))) {
            ++cluster.supportCount;
            if (currentScore > cluster.bestScore) {
                cluster.bestScore = currentScore;
                cluster.bestResult = result;
                cluster.signature = signature;
            }
            return;
        }
    }

    SolutionCluster cluster;
    cluster.bestResult = result;
    cluster.signature = signature;
    cluster.bestScore = currentScore;
    cluster.supportCount = 1;
    solutionClusters->push_back(cluster);
}

int quantizeTriangleRatio(double ratio, double ratioTolerance)
{
    const double binSize = std::max(1e-6, ratioTolerance);
    return static_cast<int>(std::floor(ratio / binSize));
}

TriangleKey triangleKeyForInvariant(const TriangleInvariant& invariant,
                                    const PatternMatcherConfig& config)
{
    return TriangleKey{
        quantizeTriangleRatio(invariant.r1, config.ratioTolerance),
        quantizeTriangleRatio(invariant.r2, config.ratioTolerance),
    };
}

QHash<TriangleKey, QVector<CatalogTriangleRecord>> buildCatalogTriangleIndex(
    const QVector<PatternCatalogPoint>& catalogPoints,
    const PatternMatcherConfig& config)
{
    QHash<TriangleKey, QVector<CatalogTriangleRecord>> index;
    forEachTriangle(catalogPoints.size(), [&](TriangleIndex catalogTriangle) {
        const QPointF catalog[3] = {
            catalogPoints[catalogTriangle.a].planeRad,
            catalogPoints[catalogTriangle.b].planeRad,
            catalogPoints[catalogTriangle.c].planeRad,
        };
        const TriangleInvariant invariant =
            makeTriangleInvariant(catalog[0], catalog[1], catalog[2]);
        if (!invariant.valid) {
            return;
        }
        index[triangleKeyForInvariant(invariant, config)].push_back(
            CatalogTriangleRecord{catalogTriangle.a,
                                  catalogTriangle.b,
                                  catalogTriangle.c,
                                  invariant});
    });
    return index;
}

QVector<CatalogTriangleRecord> findCatalogTriangleCandidates(
    const QHash<TriangleKey, QVector<CatalogTriangleRecord>>& catalogTriangleIndex,
    const TriangleInvariant& detectedInvariant,
    const PatternMatcherConfig& config)
{
    QVector<CatalogTriangleRecord> candidates;
    if (config.cancelled && config.cancelled->load()) {
        return candidates;
    }
    const TriangleKey centerKey = triangleKeyForInvariant(detectedInvariant, config);
    for (int r1Offset = -1; r1Offset <= 1; ++r1Offset) {
        for (int r2Offset = -1; r2Offset <= 1; ++r2Offset) {
            if (config.cancelled && config.cancelled->load()) {
                return candidates;
            }
            const TriangleKey key{centerKey.r1Bin + r1Offset, centerKey.r2Bin + r2Offset};
            const QVector<CatalogTriangleRecord> records = catalogTriangleIndex.value(key);
            for (const CatalogTriangleRecord& record : records) {
                if (std::abs(detectedInvariant.r1 - record.invariant.r1) > config.ratioTolerance ||
                    std::abs(detectedInvariant.r2 - record.invariant.r2) > config.ratioTolerance) {
                    continue;
                }
                candidates.push_back(record);
                if (candidates.size() >= config.maxCatalogTrianglesPerImageTriangle) {
                    return candidates;
                }
            }
        }
    }
    return candidates;
}
}

TriangleInvariant makeTriangleInvariant(const QPointF& a,
                                         const QPointF& b,
                                         const QPointF& c)
{
    std::array<double, 3> distances = {
        distanceBetween(a, b),
        distanceBetween(b, c),
        distanceBetween(c, a),
    };
    std::sort(distances.begin(), distances.end());

    TriangleInvariant invariant;
    invariant.signedArea = triangleSignedArea(a, b, c);
    invariant.longestDistance = distances[2];
    if (invariant.longestDistance <= 0.0 || std::abs(invariant.signedArea) < 1e-9) {
        return invariant;
    }

    invariant.r1 = distances[0] / invariant.longestDistance;
    invariant.r2 = distances[1] / invariant.longestDistance;
    invariant.valid = true;
    return invariant;
}

QPointF applySimilarityTransform(const SimilarityTransform2D& transform,
                                 const QPointF& point)
{
    const double sourceX = transform.mirrored ? -point.x() : point.x();
    const double sourceY = point.y();
    const double c = std::cos(transform.rotationRad);
    const double s = std::sin(transform.rotationRad);
    return QPointF(transform.scale * (c * sourceX - s * sourceY) + transform.translation.x(),
                   transform.scale * (s * sourceX + c * sourceY) + transform.translation.y());
}

SimilarityTransform2D makeSimilarityTransformFromPairs(const QPointF& sourceA,
                                                       const QPointF& sourceB,
                                                       const QPointF& targetA,
                                                       const QPointF& targetB,
                                                       bool mirrored)
{
    QPointF adjustedA = sourceA;
    QPointF adjustedB = sourceB;
    if (mirrored) {
        adjustedA.setX(-adjustedA.x());
        adjustedB.setX(-adjustedB.x());
    }

    const QPointF sourceDelta = adjustedB - adjustedA;
    const QPointF targetDelta = targetB - targetA;
    const double sourceLength = distanceBetween(adjustedA, adjustedB);
    const double targetLength = distanceBetween(targetA, targetB);

    SimilarityTransform2D transform;
    transform.mirrored = mirrored;
    if (sourceLength <= 0.0) {
        return transform;
    }

    transform.scale = targetLength / sourceLength;
    transform.rotationRad = std::atan2(targetDelta.y(), targetDelta.x()) -
                            std::atan2(sourceDelta.y(), sourceDelta.x());
    transform.translation = targetA - applySimilarityTransform(transform, sourceA);
    return transform;
}

PatternMatchResult StarPatternMatcher::matchDetectedToCatalog(
    const QVector<QPointF>& detectedPoints,
    const QVector<PatternCatalogPoint>& catalogPoints,
    const PatternMatcherConfig& config) const
{
    PatternMatchResult bestResult;
    if (config.cancelled && config.cancelled->load()) {
        return bestResult;
    }
    QVector<SolutionCluster> solutionClusters;
    int testedTransformCount = 0;
    const QHash<TriangleKey, QVector<CatalogTriangleRecord>> catalogTriangleIndex =
        buildCatalogTriangleIndex(catalogPoints, config);
    for (const QVector<CatalogTriangleRecord>& records : catalogTriangleIndex) {
        bestResult.catalogTriangleCount += records.size();
    }

    const auto considerCandidateTransform = [&](const SimilarityTransform2D& transform) {
        if (config.cancelled && config.cancelled->load()) {
            return;
        }
        if (testedTransformCount >= config.maxTestedTransforms) {
            return;
        }
        ++testedTransformCount;
        PatternMatchResult result = matchPoints(detectedPoints,
                                                catalogPoints,
                                                transform,
                                                config.initialMaxResidualPx,
                                                config.maxResidualPx,
                                                config.minMatchedStars);
        addResultToSolutionClusters(&solutionClusters, result, catalogPoints);
    };

    forEachTriangle(detectedPoints.size(), [&](TriangleIndex detectedTriangle) {
        if (config.cancelled && config.cancelled->load()) {
            return;
        }
        if (testedTransformCount >= config.maxTestedTransforms) {
            return;
        }
        const QPointF detected[3] = {
            detectedPoints[detectedTriangle.a],
            detectedPoints[detectedTriangle.b],
            detectedPoints[detectedTriangle.c],
        };
        const TriangleInvariant detectedInvariant =
            makeTriangleInvariant(detected[0], detected[1], detected[2]);
        if (!detectedInvariant.valid ||
            detectedInvariant.longestDistance < config.minTriangleLongestPx) {
            return;
        }
        ++bestResult.imageTriangleCount;

        const QVector<CatalogTriangleRecord> catalogCandidates =
            findCatalogTriangleCandidates(catalogTriangleIndex, detectedInvariant, config);
        bestResult.candidateTriangleCount += catalogCandidates.size();
        for (const CatalogTriangleRecord& catalogTriangle : catalogCandidates) {
            const double plateScaleArcsecPx =
                estimatePlateScaleArcsecPx(catalogTriangle.invariant.longestDistance,
                                           detectedInvariant.longestDistance);
            if (plateScaleArcsecPx < config.minPlateScaleArcsecPx ||
                plateScaleArcsecPx > config.maxPlateScaleArcsecPx) {
                continue;
            }

            const int catalogIndexes[3] = {
                catalogTriangle.a,
                catalogTriangle.b,
                catalogTriangle.c,
            };
            const int detectedIndexes[3] = {
                detectedTriangle.a,
                detectedTriangle.b,
                detectedTriangle.c,
            };
            std::array<int, 3> permutation = {0, 1, 2};
            const bool mirrorVariants[2] = {
                false, // mirrored = false
                true   // mirrored = true
            };
            do {
                for (bool mirrored : mirrorVariants) {
                    SimilarityTransform2D transform =
                        makeSimilarityTransformFromPairs(
                            catalogPoints[catalogIndexes[permutation[0]]].planeRad,
                            catalogPoints[catalogIndexes[permutation[1]]].planeRad,
                            detectedPoints[detectedIndexes[0]],
                            detectedPoints[detectedIndexes[1]],
                            mirrored);
                    considerCandidateTransform(transform);
                }
            } while (std::next_permutation(permutation.begin(), permutation.end()));
        }
    });
    double bestScore = -std::numeric_limits<double>::infinity();
    double secondBestClusterScore = -std::numeric_limits<double>::infinity();
    int bestClusterIndex = -1;
    int secondBestClusterIndex = -1;
    for (int i = 0; i < solutionClusters.size(); ++i) {
        const double clusterScore = solutionClusters[i].bestScore;
        if (clusterScore > bestScore) {
            secondBestClusterScore = bestScore;
            secondBestClusterIndex = bestClusterIndex;
            bestScore = clusterScore;
            bestClusterIndex = i;
        } else if (clusterScore > secondBestClusterScore) {
            secondBestClusterScore = clusterScore;
            secondBestClusterIndex = i;
        }
    }

    const int catalogTriangleCount = bestResult.catalogTriangleCount;
    const int imageTriangleCount = bestResult.imageTriangleCount;
    const int candidateTriangleCount = bestResult.candidateTriangleCount;
    if (bestClusterIndex >= 0) {
        bestResult = solutionClusters[bestClusterIndex].bestResult;
    }
    bestResult.catalogTriangleCount = catalogTriangleCount;
    bestResult.imageTriangleCount = imageTriangleCount;
    bestResult.candidateTriangleCount = candidateTriangleCount;
    bestResult.testedTransformCount = testedTransformCount;
    bestResult.solutionClusterCount = solutionClusters.size();
    bestResult.bestSolutionSupportCount =
        bestClusterIndex >= 0 ? solutionClusters[bestClusterIndex].supportCount : 0;
    bestResult.secondBestSolutionSupportCount =
        secondBestClusterIndex >= 0 ? solutionClusters[secondBestClusterIndex].supportCount : 0;
    bestResult.bestScore = std::isfinite(bestScore) ? bestScore : 0.0;
    if (std::isfinite(secondBestClusterScore)) {
        bestResult.secondBestScore = secondBestClusterScore;
        bestResult.scoreMargin = bestScore - secondBestClusterScore;
    } else {
        bestResult.secondBestScore = 0.0;
        bestResult.scoreMargin = std::numeric_limits<double>::infinity();
    }
    annotateSolvedReferencePoints(catalogPoints, &bestResult);
    return bestResult;
}

PatternMatchResult StarPatternMatcher::matchPoints(
    const QVector<QPointF>& detectedPoints,
    const QVector<PatternCatalogPoint>& catalogPoints,
    const SimilarityTransform2D& initialTransform,
    double initialMaxResidualPx,
    double refinedMaxResidualPx,
    int minMatchedStars) const
{
    PatternMatchResult result;
    const QVector<PatternMatchPair> initialPairs =
        collectNearestOneToOneMatches(detectedPoints,
                                      catalogPoints,
                                      initialTransform,
                                      initialMaxResidualPx);
    if (initialPairs.size() < minMatchedStars) {
        result.transform = initialTransform;
        result.pairs = initialPairs;
        result.matchedCount = result.pairs.size();
        result.rmsPx = computeRmsPx(result.pairs);
        result.maxResidualPx = computeMaxResidualPx(result.pairs);
        result.matchedSpatialSpreadPx = computeMatchedSpatialSpreadPx(result.pairs);
        result.valid = false;
        return result;
    }

    SimilarityTransform2D refinedTransform =
        fitSimilarityTransform(catalogPoints, initialPairs, initialTransform);
    refinedTransform.mirrored = initialTransform.mirrored;
    result.transform = refinedTransform;
    result.pairs = collectNearestOneToOneMatches(detectedPoints,
                                                 catalogPoints,
                                                 refinedTransform,
                                                 refinedMaxResidualPx);
    result.matchedCount = result.pairs.size();
    result.rmsPx = computeRmsPx(result.pairs);
    result.maxResidualPx = computeMaxResidualPx(result.pairs);
    result.matchedSpatialSpreadPx = computeMatchedSpatialSpreadPx(result.pairs);
    result.valid = result.matchedCount >= minMatchedStars && std::isfinite(result.rmsPx);
    annotateSolvedReferencePoints(catalogPoints, &result);
    return result;
}

QVector<PatternMatchPair> StarPatternMatcher::collectNearestOneToOneMatches(
    const QVector<QPointF>& detectedPoints,
    const QVector<PatternCatalogPoint>& catalogPoints,
    const SimilarityTransform2D& transform,
    double maxResidualPx) const
{
    QVector<PatternMatchPair> pairs;
    QVector<MatchCandidateEdge> candidateEdges;
    candidateEdges.reserve(catalogPoints.size() * detectedPoints.size());
    for (int catalogCandidateIndex = 0; catalogCandidateIndex < catalogPoints.size(); ++catalogCandidateIndex) {
        const QPointF predictedPixel =
            applySimilarityTransform(transform, catalogPoints[catalogCandidateIndex].planeRad);
        for (int detectedIndex = 0; detectedIndex < detectedPoints.size(); ++detectedIndex) {
            const double residualPx = distanceBetween(predictedPixel, detectedPoints[detectedIndex]);
            if (residualPx > maxResidualPx) {
                continue;
            }
            MatchCandidateEdge edge;
            edge.catalogIndex = catalogCandidateIndex;
            edge.detectedIndex = detectedIndex;
            edge.predictedPixel = predictedPixel;
            edge.residualPx = residualPx;
            candidateEdges.push_back(edge);
        }
    }
    std::sort(candidateEdges.begin(),
              candidateEdges.end(),
              [](const MatchCandidateEdge& a, const MatchCandidateEdge& b) {
                  return a.residualPx < b.residualPx;
              });

    QVector<bool> usedCatalog(catalogPoints.size(), false);
    QVector<bool> usedDetected(detectedPoints.size(), false);
    for (const MatchCandidateEdge& edge : candidateEdges) {
        if (usedCatalog[edge.catalogIndex] || usedDetected[edge.detectedIndex]) {
            continue;
        }
        usedCatalog[edge.catalogIndex] = true;
        usedDetected[edge.detectedIndex] = true;
        PatternMatchPair pair;
        pair.detectedIndex = edge.detectedIndex;
        pair.catalogIndex = edge.catalogIndex;
        pair.detectedPixel = detectedPoints[edge.detectedIndex];
        pair.predictedPixel = edge.predictedPixel;
        pair.residualPx = edge.residualPx;
        pairs.push_back(pair);
    }
    return pairs;
}

SimilarityTransform2D StarPatternMatcher::fitSimilarityTransform(
    const QVector<PatternCatalogPoint>& catalogPoints,
    const QVector<PatternMatchPair>& pairs,
    const SimilarityTransform2D& initialTransform) const
{
    if (pairs.size() < 2) {
        return initialTransform;
    }

    QPointF sourceCentroid;
    QPointF targetCentroid;
    for (const PatternMatchPair& pair : pairs) {
        QPointF source = catalogPoints[pair.catalogIndex].planeRad;
        if (initialTransform.mirrored) {
            source.setX(-source.x());
        }
        sourceCentroid += source;
        targetCentroid += pair.detectedPixel;
    }
    sourceCentroid /= pairs.size();
    targetCentroid /= pairs.size();

    double weightedSourceNorm = 0.0;
    double dot = 0.0;
    double cross = 0.0;
    for (const PatternMatchPair& pair : pairs) {
        QPointF source = catalogPoints[pair.catalogIndex].planeRad;
        if (initialTransform.mirrored) {
            source.setX(-source.x());
        }
        const QPointF sourceDelta = source - sourceCentroid;
        const QPointF targetDelta = pair.detectedPixel - targetCentroid;
        weightedSourceNorm += sourceDelta.x() * sourceDelta.x() +
                              sourceDelta.y() * sourceDelta.y();
        dot += sourceDelta.x() * targetDelta.x() + sourceDelta.y() * targetDelta.y();
        cross += sourceDelta.x() * targetDelta.y() - sourceDelta.y() * targetDelta.x();
    }
    if (weightedSourceNorm <= 0.0) {
        return initialTransform;
    }

    SimilarityTransform2D refinedTransform;
    refinedTransform.mirrored = initialTransform.mirrored;
    refinedTransform.scale = std::hypot(dot, cross) / weightedSourceNorm;
    refinedTransform.rotationRad = std::atan2(cross, dot);
    const double c = std::cos(refinedTransform.rotationRad);
    const double s = std::sin(refinedTransform.rotationRad);
    const QPointF rotatedSource(refinedTransform.scale *
                                    (c * sourceCentroid.x() - s * sourceCentroid.y()),
                                refinedTransform.scale *
                                    (s * sourceCentroid.x() + c * sourceCentroid.y()));
    refinedTransform.translation = targetCentroid - rotatedSource;
    return refinedTransform;
}

void StarPatternMatcher::annotateSolvedReferencePoints(
    const QVector<PatternCatalogPoint>& catalogPoints,
    PatternMatchResult* solvedResult)
{
    if (!solvedResult || !solvedResult->valid) {
        return;
    }

    PatternMatchResult& result = *solvedResult;
    result.northCelestialPolePixel =
        applySimilarityTransform(result.transform, QPointF(0.0, 0.0));
    result.hasNorthCelestialPolePixel = true;
    for (int i = 0; i < catalogPoints.size(); ++i) {
        if (!catalogPoints[i].isPolaris) {
            continue;
        }
        result.polarisPixel =
            applySimilarityTransform(result.transform, catalogPoints[i].planeRad);
        result.hasPolarisPixel = true;
        result.polarisPolarRadiusPx =
            distanceBetween(result.polarisPixel, result.northCelestialPolePixel);
        break;
    }
}

double StarPatternMatcher::computeRmsPx(const QVector<PatternMatchPair>& pairs)
{
    if (pairs.isEmpty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double sumSquares = 0.0;
    for (const PatternMatchPair& pair : pairs) {
        sumSquares += pair.residualPx * pair.residualPx;
    }
    return std::sqrt(sumSquares / pairs.size());
}

double StarPatternMatcher::computeMaxResidualPx(const QVector<PatternMatchPair>& pairs)
{
    double maxResidual = 0.0;
    for (const PatternMatchPair& pair : pairs) {
        if (std::isfinite(pair.residualPx)) {
            maxResidual = std::max(maxResidual, pair.residualPx);
        }
    }
    return maxResidual;
}

double StarPatternMatcher::computeMatchedSpatialSpreadPx(const QVector<PatternMatchPair>& pairs)
{
    if (pairs.isEmpty()) {
        return 0.0;
    }

    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    for (const PatternMatchPair& pair : pairs) {
        minX = std::min(minX, pair.detectedPixel.x());
        minY = std::min(minY, pair.detectedPixel.y());
        maxX = std::max(maxX, pair.detectedPixel.x());
        maxY = std::max(maxY, pair.detectedPixel.y());
    }
    if (!std::isfinite(minX) || !std::isfinite(minY) ||
        !std::isfinite(maxX) || !std::isfinite(maxY)) {
        return 0.0;
    }
    return std::hypot(maxX - minX, maxY - minY);
}
