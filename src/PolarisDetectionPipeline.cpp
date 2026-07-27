#include "PolarisDetectionPipeline.h"

#include <algorithm>
#include <cmath>

#include <QRectF>

PolarisDetectionPipeline::InitialStarSelection PolarisDetectionPipeline::selectInitialStarCandidate(
    QVector<InitialStarCandidate> candidates,
    bool hasPreference,
    const QPointF& preference,
    int selectedCandidateIndex)
{
    InitialStarSelection selection;
    if (candidates.isEmpty()) {
        selection.reason = QStringLiteral("No initial star candidates detected");
        return selection;
    }

    if (selectedCandidateIndex > 0) {
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

    if (hasPreference) {
        for (InitialStarCandidate& candidate : candidates) {
            const QPointF delta = candidate.center - preference;
            candidate.distanceToPreference = std::hypot(delta.x(), delta.y());
        }
        const auto best = std::min_element(candidates.cbegin(), candidates.cend(),
                                           [](const InitialStarCandidate& a,
                                              const InitialStarCandidate& b) {
            return a.distanceToPreference < b.distanceToPreference;
        });
        if (best != candidates.cend() && best->distanceToPreference <= 128.0) {
            selection.selected = true;
            selection.candidate = *best;
            return selection;
        }
        if (candidates.size() == 1) {
            selection.selected = true;
            selection.candidate = candidates.first();
            selection.reason = QStringLiteral("Single candidate accepted after target motion");
            return selection;
        }
        const InitialStarCandidate& strongest = candidates.first();
        const double nextSignal = candidates.size() > 1
                                      ? std::max(1.0, candidates.at(1).signal)
                                      : 1.0;
        if (strongest.area >= 6 && strongest.peak >= 24.0 &&
            strongest.signal >= nextSignal * 2.0) {
            selection.selected = true;
            selection.candidate = strongest;
            selection.reason = QStringLiteral("Dominant candidate accepted after target motion");
            return selection;
        }
        selection.requiresUserSelection = true;
        selection.reason = QStringLiteral("Nearest candidate is too far from the last target position");
        return selection;
    }

    if (candidates.size() == 1) {
        selection.selected = true;
        selection.candidate = candidates.first();
        return selection;
    }

    selection.requiresUserSelection = true;
    selection.reason = QStringLiteral("Multiple star candidates detected; confirm the Polaris candidate index");
    return selection;
}

bool PolarisDetectionPipeline::chooseAutomaticInitialStarCandidate(
    const QVector<InitialStarCandidate>& candidates,
    const InitialStarCandidate& strongestCandidate,
    InitialStarCandidate* selected,
    QString* reason)
{
    if (candidates.isEmpty() || !selected) {
        if (reason) {
            *reason = QStringLiteral("未检测到可用于自动定位的候选星点");
        }
        return false;
    }

    if (strongestCandidate.area < 6 || strongestCandidate.peak < 24.0) {
        if (reason) {
            *reason = QStringLiteral("最强候选星点过弱或面积过小，继续全画幅定位");
        }
        return false;
    }

    if (candidates.size() > 1) {
        const double nextSignal = std::max(1.0, candidates.at(1).signal);
        if (strongestCandidate.signal < nextSignal * 2.0) {
            if (reason) {
                *reason = QStringLiteral("检测到多个亮星且最强候选不够突出，请进入对准模式确认北极星");
            }
            return false;
        }
    }

    *selected = strongestCandidate;
    return true;
}

QVector<FullFrameCanvas::StarCandidateOverlay>
PolarisDetectionPipeline::buildCandidateOverlays(const QVector<InitialStarCandidate>& candidates,
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
