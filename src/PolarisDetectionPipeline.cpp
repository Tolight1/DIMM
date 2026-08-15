#include "PolarisDetectionPipeline.h"

#include <QRectF>

#include <cmath>
#include <limits>

PolarisDetectionPipeline::InitialStarSelection
PolarisDetectionPipeline::selectFullFrameStarCandidate(
    const QVector<InitialStarCandidate>& candidates,
    int selectedCandidateIndex,
    bool manualSelectionConfirmed)
{
    InitialStarSelection selection;
    if (candidates.isEmpty()) {
        selection.reason = QStringLiteral("No initial star candidates detected");
        return selection;
    }

    // Full-frame localization deliberately uses only candidate count. A single
    // candidate is accepted directly, while multiple candidates require the
    // existing manual candidate-index flow.
    if (candidates.size() == 1) {
        selection.selected = true;
        selection.candidate = candidates.first();
        return selection;
    }

    if (manualSelectionConfirmed && selectedCandidateIndex > 0) {
        for (const InitialStarCandidate& candidate : candidates) {
            if (candidate.index == selectedCandidateIndex) {
                selection.selected = true;
                selection.candidate = candidate;
                return selection;
            }
        }
        selection.requiresUserSelection = true;
        selection.reason = QStringLiteral("Selected candidate index is not in the current candidate list");
        return selection;
    }

    selection.requiresUserSelection = true;
    selection.reason = QStringLiteral("Multiple star candidates detected; confirm the Polaris candidate index");
    return selection;
}

PolarisDetectionPipeline::InitialStarSelection
PolarisDetectionPipeline::selectNearestCandidate(
    const QVector<InitialStarCandidate>& candidates,
    const QPointF& target,
    double maxDistancePx)
{
    InitialStarSelection selection;
    if (candidates.isEmpty() || !std::isfinite(maxDistancePx) || maxDistancePx <= 0.0) {
        selection.reason = QStringLiteral("No candidate is near the confirmed target");
        return selection;
    }

    const double maxDistanceSquared = maxDistancePx * maxDistancePx;
    double nearestDistanceSquared = std::numeric_limits<double>::max();
    for (const InitialStarCandidate& candidate : candidates) {
        const double dx = candidate.center.x() - target.x();
        const double dy = candidate.center.y() - target.y();
        const double distanceSquared = dx * dx + dy * dy;
        if (distanceSquared < nearestDistanceSquared) {
            nearestDistanceSquared = distanceSquared;
            selection.candidate = candidate;
        }
    }

    if (nearestDistanceSquared <= maxDistanceSquared) {
        selection.selected = true;
        return selection;
    }

    selection.reason = QStringLiteral("No candidate is near the confirmed target");
    return selection;
}

QVector<FullFrameCanvas::StarCandidateOverlay>
PolarisDetectionPipeline::buildCandidateOverlays(
    const QVector<InitialStarCandidate>& candidates,
    int selectedIndex)
{
    QVector<FullFrameCanvas::StarCandidateOverlay> overlays;
    overlays.reserve(candidates.size());
    for (const InitialStarCandidate& candidate : candidates) {
        FullFrameCanvas::StarCandidateOverlay overlay;
        overlay.index = candidate.index;
        overlay.center = candidate.center;
        overlay.bbox = QRectF(candidate.bbox);
        overlay.selected = candidate.index == selectedIndex;
        overlays.append(overlay);
    }
    return overlays;
}

QVector<PolarisDetectionPipeline::InitialStarCandidate>
PolarisDetectionPipeline::initialCandidatesFromPolarisDetections(
    const QVector<DetectedStar>& detections)
{
    QVector<InitialStarCandidate> candidates;
    candidates.reserve(detections.size());
    for (int i = 0; i < detections.size(); ++i) {
        const DetectedStar& detection = detections[i];
        InitialStarCandidate candidate;
        candidate.index = detection.detectionIndex > 0 ? detection.detectionIndex : i + 1;
        candidate.center = detection.centroidPx;
        candidate.area = detection.areaPx;
        candidate.peak = detection.peak;
        candidate.signal = detection.flux;
        candidate.bbox = detection.boundingBoxPx.toAlignedRect();
        candidates.push_back(candidate);
    }
    return candidates;
}
