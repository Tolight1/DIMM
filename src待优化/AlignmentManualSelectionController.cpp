#include "AlignmentManualSelectionController.h"

#include <cmath>

bool AlignmentManualSelectionController::canApplyCandidateSelection(
    bool manualSelectionRequested,
    bool hadConfirmedPolarisBeforeSelection)
{
    return manualSelectionRequested || hadConfirmedPolarisBeforeSelection;
}

bool AlignmentManualSelectionController::shouldShowCandidatePrompt(qint64 lastPromptMs,
                                                                   qint64 nowMs,
                                                                   qint64 promptIntervalMs)
{
    return lastPromptMs < 0 || nowMs - lastPromptMs >= promptIntervalMs;
}

void AlignmentManualSelectionController::recordCandidatePromptCancelled(qint64* lastPromptMs,
                                                                        qint64 nowMs)
{
    if (lastPromptMs) {
        *lastPromptMs = nowMs;
    }
}

void AlignmentManualSelectionController::recordCandidatePromptAccepted(
    int* selectedCandidateIndex,
    qint64* lastPromptMs,
    int chosenCandidateIndex)
{
    if (selectedCandidateIndex) {
        *selectedCandidateIndex = chosenCandidateIndex;
    }
    if (lastPromptMs) {
        *lastPromptMs = -1;
    }
}

void AlignmentManualSelectionController::applyConfirmedCandidate(
    const QPointF& selectedPosition,
    int selectedCandidateIndex,
    QPointF* confirmedPosition,
    bool* hasConfirmedPosition,
    QPointF* lastTargetPosition,
    bool* hasLastTargetPosition,
    bool* pendingSelectionRequired,
    int* selectedInitialCandidateIndex,
    bool* selectionRequested)
{
    if (confirmedPosition) {
        *confirmedPosition = selectedPosition;
    }
    if (hasConfirmedPosition) {
        *hasConfirmedPosition = true;
    }
    if (lastTargetPosition) {
        *lastTargetPosition = selectedPosition;
    }
    if (hasLastTargetPosition) {
        *hasLastTargetPosition = true;
    }
    if (pendingSelectionRequired) {
        *pendingSelectionRequired = false;
    }
    if (selectedInitialCandidateIndex) {
        *selectedInitialCandidateIndex = selectedCandidateIndex;
    }
    if (selectionRequested) {
        *selectionRequested = false;
    }
}

void AlignmentManualSelectionController::applyManualConfirmation(
    AlignmentCameraSolveRuntime* runtime,
    const QPointF& selectedPosition)
{
    if (!runtime) {
        return;
    }

    runtime->state = AlignmentSolveState::ManualOnly;
    runtime->nextRetryMs = -1;
    runtime->consecutiveTrackFailures = 0;
    runtime->consecutiveLowConfidenceResults = 0;
    runtime->lastPolarisPosition = selectedPosition;
    runtime->hasLastPolarisPosition = true;
}

QString AlignmentManualSelectionController::manualConfirmedMessage(
    const QPointF& selectedPosition)
{
    return QStringLiteral("(%1, %2)")
        .arg(selectedPosition.x(), 0, 'f', 1)
        .arg(selectedPosition.y(), 0, 'f', 1);
}

QStringList AlignmentManualSelectionController::candidatePromptLines(
    const QVector<PolarisDetectionPipeline::InitialStarCandidate>& candidates)
{
    QStringList candidateLines;
    candidateLines.reserve(candidates.size());
    for (const PolarisDetectionPipeline::InitialStarCandidate& candidate : candidates) {
        const QString distanceText =
            std::isfinite(candidate.distanceToPreference)
                ? QString::number(candidate.distanceToPreference, 'f', 1)
                : QStringLiteral("--");
        candidateLines << QStringLiteral("候选 %1: 中心=(%2, %3), 面积=%4, 峰值=%5, 距离上次=%6 px")
                              .arg(candidate.index)
                              .arg(candidate.center.x(), 0, 'f', 1)
                              .arg(candidate.center.y(), 0, 'f', 1)
                              .arg(candidate.area)
                              .arg(candidate.peak, 0, 'f', 1)
                              .arg(distanceText);
    }
    return candidateLines;
}
