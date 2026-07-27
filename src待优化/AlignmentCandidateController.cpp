#include "AlignmentCandidateController.h"

QVector<AlignmentCandidateController::InitialStarCandidate>
AlignmentCandidateController::collectCandidates(const CandidateCollectionInput& input,
                                                cv::Mat* mono8,
                                                double* peakValue)
{
    if (!input.frame || !input.solved) {
        return {};
    }

    const PolarisSolveResult& solved = *input.solved;
    const bool canReuseSolverDetections =
        input.autoSolveEnabled &&
        input.hasCurrentSolverResult &&
        solved.frameId == input.lastFrameId &&
        !solved.detections.isEmpty();
    if (canReuseSolverDetections) {
        return PolarisDetectionPipeline::initialCandidatesFromPolarisDetections(solved.detections);
    }
    if (!input.allowGuiCandidateDetection || !input.candidateDetector) {
        return {};
    }

    cv::Mat grayscale;
    if (input.frame->channels() == 1) {
        grayscale = *input.frame;
    } else {
        cv::cvtColor(*input.frame, grayscale, cv::COLOR_BGR2GRAY);
    }
    if (mono8) {
        double ignoredPeak = 0.0;
        return input.candidateDetector(grayscale, peakValue ? peakValue : &ignoredPeak);
    }
    double ignoredPeak = 0.0;
    return input.candidateDetector(grayscale, peakValue ? peakValue : &ignoredPeak);
}

AlignmentCandidateController::InitialStarSelection
AlignmentCandidateController::selectInitialCandidate(
    const RuntimeAccess& runtime,
    const QVector<InitialStarCandidate>& candidates,
    bool manualSelectionRequested,
    QPointF* preferredTarget)
{
    const bool hasConfirmed = runtime.hasConfirmedPolarisPosition &&
                              *runtime.hasConfirmedPolarisPosition;
    const bool hasLastTarget = runtime.hasLastTargetPosition &&
                               *runtime.hasLastTargetPosition;
    const bool hasPreferredTarget = !manualSelectionRequested && (hasConfirmed || hasLastTarget);
    const QPointF target = hasConfirmed && runtime.confirmedPolarisPosition
                               ? *runtime.confirmedPolarisPosition
                               : (runtime.lastTargetPosition ? *runtime.lastTargetPosition
                                                             : QPointF());
    if (preferredTarget) {
        *preferredTarget = target;
    }
    return PolarisDetectionPipeline::selectInitialStarCandidate(
        candidates,
        hasPreferredTarget,
        target,
        runtime.selectedInitialCandidateIndex ? *runtime.selectedInitialCandidateIndex : -1);
}

void AlignmentCandidateController::recordSelectedCandidate(RuntimeAccess runtime,
                                                           const QPointF& star,
                                                           int selectedCandidateIndex)
{
    if (runtime.confirmedPolarisPosition) {
        *runtime.confirmedPolarisPosition = star;
    }
    if (runtime.hasConfirmedPolarisPosition) {
        *runtime.hasConfirmedPolarisPosition = true;
    }
    if (runtime.lastTargetPosition) {
        *runtime.lastTargetPosition = star;
    }
    if (runtime.hasLastTargetPosition) {
        *runtime.hasLastTargetPosition = true;
    }
    if (runtime.pendingInitialCandidateSelectionRequired) {
        *runtime.pendingInitialCandidateSelectionRequired = false;
    }
    if (runtime.selectedInitialCandidateIndex) {
        *runtime.selectedInitialCandidateIndex = selectedCandidateIndex;
    }
    if (runtime.selectionRequested) {
        *runtime.selectionRequested = false;
    }
}

void AlignmentCandidateController::updateFromFallbackCentroid(
    RuntimeAccess runtime,
    const cv::Mat& frame,
    bool allowGuiCandidateDetection,
    cv::Mat* mono8,
    QPointF* star,
    double* peakValue,
    const CentroidDetector& centroidDetector)
{
    if (!allowGuiCandidateDetection ||
        !runtime.hasConfirmedPolarisPosition ||
        !*runtime.hasConfirmedPolarisPosition ||
        !mono8 ||
        !star ||
        !centroidDetector) {
        return;
    }

    if (mono8->empty()) {
        cv::Mat grayscale;
        if (frame.channels() == 1) {
            grayscale = frame;
        } else {
            cv::cvtColor(frame, grayscale, cv::COLOR_BGR2GRAY);
        }
        *mono8 = grayscale;
    }
    if (mono8->empty() || !centroidDetector(*mono8, star, peakValue)) {
        return;
    }

    recordSelectedCandidate(runtime, *star,
                            runtime.selectedInitialCandidateIndex
                                ? *runtime.selectedInitialCandidateIndex
                                : -1);
}
