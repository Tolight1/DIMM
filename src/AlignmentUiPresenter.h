#pragma once

#include "AlignmentTypes.h"
#include "CanvasWidgets.h"
#include "PolarisSolver.h"

#include <QDateTime>
#include <QSize>
#include <QString>

namespace AlignmentUiPresenter {
struct OverlayBuildInput {
    QSize frameSize;
    double fallbackOrbitRadiusPx = 0.0;
    double fallbackPlateScaleArcsecPx = 0.0;
    bool useSolvedOrbit = true;
    double radiusAdjustPx = 0.0;
    AlignmentSolveState solveState = AlignmentSolveState::WaitingFrame;
    const PolarisSolveResult* solved = nullptr;
    bool hasCurrentSolverResult = false;
    bool hasConfirmedPolarisForSimulation = false;
    QPointF confirmedPolarisForSimulation;
    QDateTime confirmedPolarisTimeUtc;
    double simulationPhaseAtConfirmationRad = 0.0;
    bool hasSimulationPhase = false;
    QDateTime simulationNowUtc;
};

QString solveStateText(AlignmentSolveState state);
QString polarisSolveStatusText(PolarisSolveStatus status);
QString waitingAlignmentLabelText();
QString stoppedAlignmentLabelText();
QString startedAlignmentStatusText(double previewRateHz);
QString stoppedAlignmentStatusText();
QString formatManualConfirmedSolveLabel(const QString& message);
QString formatManualConfirmedStatusMessage(int cameraIndex, const QString& message);
QString formatMatchingSolveLabel(const QString& message);
QString formatMatchingStatusMessage(int cameraIndex, const QString& message);
QString formatManualTrackingSolveLabel(const QPointF& trackedPosition);
QString formatManualTrackingLostSolveLabel();
QString formatTrackingSolveLabel(const QPointF& trackedPosition);
QString formatTrackingLostSolveLabel(int consecutiveTrackFailures, int lostTrackRetryCount);
QString formatRetryWaitingSolveLabel(qint64 remainingMs);
QString formatMissingFrameSolveLabel();
QString formatMissingFrameStatusMessage(int cameraIndex);
QString formatSubmittedSolveLabel(bool force);
QString formatPredictedOnlySolveLabel(const PolarisSolveResult& result);
QString formatPredictedOnlyStatusMessage(const PolarisSolveResult& result);
QString formatSolvedSolveLabel(const PolarisSolveResult& result);
QString formatSolvedStatusMessage(const PolarisSolveResult& result);
QString formatRetrySolveLabel(const PolarisSolveResult& result);
QString formatRetryStatusMessage(const PolarisSolveResult& result, const QString& diagnosticHint);
QString formatErrorSolveLabel(const PolarisSolveResult& result);
QString formatErrorStatusMessage(const PolarisSolveResult& result);
QString formatPolarisSolveLogLine(const PolarisSolveResult& result);
FullFrameCanvas::AlignmentOverlay buildAlignmentOverlay(const OverlayBuildInput& input);
void applyConfirmedPolarisToOverlay(bool hasConfirmedPolarisPosition,
                                    const QPointF& confirmedPolarisPosition,
                                    FullFrameCanvas::AlignmentOverlay* overlay);
}
