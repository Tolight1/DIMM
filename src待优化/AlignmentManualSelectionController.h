#pragma once

#include "AlignmentTypes.h"
#include "PolarisDetectionPipeline.h"

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

class AlignmentManualSelectionController final {
public:
    static bool canApplyCandidateSelection(bool manualSelectionRequested,
                                           bool hadConfirmedPolarisBeforeSelection);
    static bool shouldShowCandidatePrompt(qint64 lastPromptMs,
                                          qint64 nowMs,
                                          qint64 promptIntervalMs = 2000);
    static void recordCandidatePromptCancelled(qint64* lastPromptMs, qint64 nowMs);
    static void recordCandidatePromptAccepted(int* selectedCandidateIndex,
                                              qint64* lastPromptMs,
                                              int chosenCandidateIndex);
    static void applyConfirmedCandidate(const QPointF& selectedPosition,
                                        int selectedCandidateIndex,
                                        QPointF* confirmedPosition,
                                        bool* hasConfirmedPosition,
                                        QPointF* lastTargetPosition,
                                        bool* hasLastTargetPosition,
                                        bool* pendingSelectionRequired,
                                        int* selectedInitialCandidateIndex,
                                        bool* selectionRequested);
    static void applyManualConfirmation(AlignmentCameraSolveRuntime* runtime,
                                        const QPointF& selectedPosition);
    static QString manualConfirmedMessage(const QPointF& selectedPosition);
    static QStringList candidatePromptLines(
        const QVector<PolarisDetectionPipeline::InitialStarCandidate>& candidates);
};
