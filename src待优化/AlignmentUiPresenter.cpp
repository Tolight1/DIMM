#include "AlignmentUiPresenter.h"

#include <algorithm>
#include <cmath>

QString AlignmentUiPresenter::solveStateText(AlignmentSolveState state)
{
    switch (state) {
    case AlignmentSolveState::Disabled:
        return QStringLiteral("自动识别关闭");
    case AlignmentSolveState::WaitingFrame:
        return QStringLiteral("等待全画幅");
    case AlignmentSolveState::FullSolving:
        return QStringLiteral("完整解算");
    case AlignmentSolveState::Tracking:
        return QStringLiteral("局部跟踪");
    case AlignmentSolveState::RetryWaiting:
        return QStringLiteral("等待重试");
    case AlignmentSolveState::ManualOnly:
        return QStringLiteral("人工确认");
    case AlignmentSolveState::Error:
        return QStringLiteral("错误");
    }
    return QStringLiteral("未知");
}

QString AlignmentUiPresenter::polarisSolveStatusText(PolarisSolveStatus status)
{
    switch (status) {
    case PolarisSolveStatus::Idle:
        return QStringLiteral("Idle");
    case PolarisSolveStatus::WaitingFrame:
        return QStringLiteral("WaitingFrame");
    case PolarisSolveStatus::DetectingStars:
        return QStringLiteral("DetectingStars");
    case PolarisSolveStatus::MatchingCatalog:
        return QStringLiteral("MatchingCatalog");
    case PolarisSolveStatus::Solved:
        return QStringLiteral("Solved");
    case PolarisSolveStatus::Tracking:
        return QStringLiteral("Tracking");
    case PolarisSolveStatus::ManualConfirmed:
        return QStringLiteral("ManualConfirmed");
    case PolarisSolveStatus::InsufficientStars:
        return QStringLiteral("InsufficientStars");
    case PolarisSolveStatus::NoCatalogMatch:
        return QStringLiteral("NoCatalogMatch");
    case PolarisSolveStatus::LowConfidence:
        return QStringLiteral("LowConfidence");
    case PolarisSolveStatus::Cancelled:
        return QStringLiteral("Cancelled");
    case PolarisSolveStatus::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

QString AlignmentUiPresenter::formatManualConfirmedSolveLabel(const QString& message)
{
    return QStringLiteral("自动识别: 人工确认 | %1").arg(message);
}

QString AlignmentUiPresenter::formatManualConfirmedStatusMessage(int cameraIndex,
                                                                 const QString& message)
{
    return QStringLiteral("状态: 相机%1北极星已人工确认: %2")
        .arg(cameraIndex + 1)
        .arg(message);
}

QString AlignmentUiPresenter::formatMatchingSolveLabel(const QString& message)
{
    return QStringLiteral("自动识别: 匹配中 | %1").arg(message);
}

QString AlignmentUiPresenter::formatMatchingStatusMessage(int cameraIndex,
                                                          const QString& message)
{
    return QStringLiteral("状态: 相机%1自动识别北极星: %2")
        .arg(cameraIndex + 1)
        .arg(message);
}

QString AlignmentUiPresenter::formatPredictedOnlySolveLabel(const PolarisSolveResult& result)
{
    return QStringLiteral("自动识别: 仅预测到北极星 | RMS %1 px | 耗时 %2 ms | 历元 %3")
        .arg(result.rmsPx, 0, 'f', 2)
        .arg(result.timing.totalMs, 0, 'f', 1)
        .arg(result.observationEpochYear, 0, 'f', 4);
}

QString AlignmentUiPresenter::formatManualTrackingSolveLabel(const QPointF& trackedPosition)
{
    return QStringLiteral("自动识别: 人工确认 | 跟踪中 | (%1, %2)")
        .arg(trackedPosition.x(), 0, 'f', 1)
        .arg(trackedPosition.y(), 0, 'f', 1);
}

QString AlignmentUiPresenter::formatManualTrackingLostSolveLabel()
{
    return QStringLiteral("自动识别: 人工确认 | 跟踪暂失，请重新确认或重新识别");
}

QString AlignmentUiPresenter::formatTrackingSolveLabel(const QPointF& trackedPosition)
{
    return QStringLiteral("自动识别: 跟踪中 | (%1, %2)")
        .arg(trackedPosition.x(), 0, 'f', 1)
        .arg(trackedPosition.y(), 0, 'f', 1);
}

QString AlignmentUiPresenter::formatTrackingLostSolveLabel(int consecutiveTrackFailures,
                                                           int lostTrackRetryCount)
{
    return QStringLiteral("自动识别: 跟踪暂失 | %1/%2")
        .arg(consecutiveTrackFailures)
        .arg(lostTrackRetryCount);
}

QString AlignmentUiPresenter::formatRetryWaitingSolveLabel(qint64 remainingMs)
{
    return QStringLiteral("自动识别: 等待重试 | %1 ms").arg(remainingMs);
}

QString AlignmentUiPresenter::formatMissingFrameSolveLabel()
{
    return QStringLiteral("自动识别: 暂无全画幅");
}

QString AlignmentUiPresenter::formatMissingFrameStatusMessage(int cameraIndex)
{
    return QStringLiteral("状态: 相机%1暂无可用于自动识别的全画幅")
        .arg(cameraIndex + 1);
}

QString AlignmentUiPresenter::formatSubmittedSolveLabel(bool force)
{
    return force ? QStringLiteral("自动识别: 已强制提交")
                 : QStringLiteral("自动识别: 已提交");
}

QString AlignmentUiPresenter::formatPredictedOnlyStatusMessage(const PolarisSolveResult& result)
{
    return QStringLiteral("状态: 相机%1星图匹配成功，但北极星未实际检测到，等待重试或人工确认；%2，历元 %3")
        .arg(result.cameraIndex + 1)
        .arg(result.coordinateModel)
        .arg(result.observationEpochYear, 0, 'f', 4);
}

QString AlignmentUiPresenter::formatSolvedSolveLabel(const PolarisSolveResult& result)
{
    return QStringLiteral("自动识别: 匹配成功 | %1 星 | RMS %2 px | %3\"/px | 旋转 %4° | 耗时 %5 ms | 历元 %6")
        .arg(result.matchedStarCount)
        .arg(result.rmsPx, 0, 'f', 2)
        .arg(result.plateScaleArcsecPx, 0, 'f', 3)
        .arg(result.rotationDeg, 0, 'f', 1)
        .arg(result.timing.totalMs, 0, 'f', 1)
        .arg(result.observationEpochYear, 0, 'f', 4);
}

QString AlignmentUiPresenter::formatSolvedStatusMessage(const PolarisSolveResult& result)
{
    return QStringLiteral("状态: 相机%1自动识别北极星成功，匹配 %2 颗星，RMS %3 px，耗时 %4 ms；%5，历元 %6")
        .arg(result.cameraIndex + 1)
        .arg(result.matchedStarCount)
        .arg(result.rmsPx, 0, 'f', 2)
        .arg(result.timing.totalMs, 0, 'f', 1)
        .arg(result.coordinateModel)
        .arg(result.observationEpochYear, 0, 'f', 4);
}

QString AlignmentUiPresenter::formatRetrySolveLabel(const PolarisSolveResult& result)
{
    return QStringLiteral("自动识别: 未匹配 | %1 | 耗时 %2 ms")
        .arg(result.message)
        .arg(result.timing.totalMs, 0, 'f', 1);
}

QString AlignmentUiPresenter::formatRetryStatusMessage(const PolarisSolveResult& result,
                                                       const QString& diagnosticHint)
{
    return QStringLiteral("状态: 相机%1自动识别北极星待重试: %2%3")
        .arg(result.cameraIndex + 1)
        .arg(result.message)
        .arg(diagnosticHint);
}

QString AlignmentUiPresenter::formatErrorSolveLabel(const PolarisSolveResult& result)
{
    return QStringLiteral("自动识别: 错误 | %1").arg(result.message);
}

QString AlignmentUiPresenter::formatErrorStatusMessage(const PolarisSolveResult& result)
{
    return QStringLiteral("状态: 相机%1自动识别北极星错误: %2")
        .arg(result.cameraIndex + 1)
        .arg(result.message);
}

QString AlignmentUiPresenter::formatPolarisSolveLogLine(const PolarisSolveResult& result)
{
    return QStringLiteral("[Polaris solve] cam=%1 status=%2 valid=%3 detected=%4 matched=%5 rms=%6 maxResidual=%7 scoreMargin=%8 spread=%9 detectionMs=%10 matchingMs=%11 totalMs=%12 mirrored=%13 message=%14 catalogStars=%15 catalogTriangles=%16 imageTriangles=%17 candidateTriangles=%18 testedTransforms=%19 solutionClusters=%20 bestClusterSupport=%21 secondClusterSupport=%22")
        .arg(result.cameraIndex + 1)
        .arg(polarisSolveStatusText(result.status))
        .arg(result.valid ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(result.detectedStarCount)
        .arg(result.matchedStarCount)
        .arg(result.rmsPx, 0, 'f', 2)
        .arg(result.maxResidualPx, 0, 'f', 2)
        .arg(result.scoreMargin, 0, 'f', 2)
        .arg(result.matchedSpatialSpreadPx, 0, 'f', 1)
        .arg(result.timing.detectionMs, 0, 'f', 1)
        .arg(result.timing.matchingMs, 0, 'f', 1)
        .arg(result.timing.totalMs, 0, 'f', 1)
        .arg(result.mirrored ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(result.message)
        .arg(result.stats.catalogStarCount)
        .arg(result.stats.catalogTriangleCount)
        .arg(result.stats.imageTriangleCount)
        .arg(result.stats.candidateTriangleCount)
        .arg(result.stats.testedTransformCount)
        .arg(result.stats.solutionClusterCount)
        .arg(result.stats.bestSolutionSupportCount)
        .arg(result.stats.secondBestSolutionSupportCount);
}

FullFrameCanvas::AlignmentOverlay AlignmentUiPresenter::buildAlignmentOverlay(
    const OverlayBuildInput& input)
{
    FullFrameCanvas::AlignmentOverlay overlay;
    overlay.enabled = true;
    overlay.orbitCenter =
        QPointF((input.frameSize.width() - 1) * 0.5, (input.frameSize.height() - 1) * 0.5);
    overlay.orbitRadiusPx = input.fallbackOrbitRadiusPx;
    overlay.plateScaleArcsecPx = input.fallbackPlateScaleArcsecPx;
    overlay.orbitSource = QStringLiteral("理论回退");
    overlay.solveStateText = solveStateText(input.solveState);

    if (!input.solved) {
        return overlay;
    }

    const PolarisSolveResult& solved = *input.solved;
    if (input.hasCurrentSolverResult &&
        solved.status == PolarisSolveStatus::LowConfidence &&
        !solved.message.isEmpty()) {
        overlay.warningText = solved.message;
    }
    if (!solved.valid ||
        !input.hasCurrentSolverResult ||
        !solved.hasNorthCelestialPolePixel) {
        return overlay;
    }

    if (input.useSolvedOrbit) {
        overlay.orbitCenter = solved.northCelestialPolePixel;
        overlay.orbitSource = QStringLiteral("解算");
    }
    overlay.matchedStarCount = solved.matchedStarCount;
    overlay.rmsPx = solved.rmsPx;
    overlay.plateScaleArcsecPx = solved.plateScaleArcsecPx;
    overlay.solveTotalMs = solved.timing.totalMs;
    overlay.mirroredKnown = true;
    overlay.mirrored = solved.mirrored;
    if (solved.showMatchedCatalogStars) {
        overlay.catalogMatches.reserve(solved.matches.size());
        for (const CatalogImageMatch& match : solved.matches) {
            FullFrameCanvas::CatalogMatchOverlay matchOverlay;
            matchOverlay.detectedPosition = match.detectedPixel;
            matchOverlay.predictedPosition = match.predictedPixel;
            matchOverlay.residualPx = match.residualPx;
            matchOverlay.isPolaris = match.isPolaris;
            matchOverlay.label =
                match.name.isEmpty()
                    ? QStringLiteral("%1 px").arg(match.residualPx, 0, 'f', 1)
                    : QStringLiteral("%1 / %2 px")
                          .arg(match.name)
                          .arg(match.residualPx, 0, 'f', 1);
            overlay.catalogMatches.push_back(matchOverlay);
        }
    }
    if (input.useSolvedOrbit && solved.polarisPolarRadiusPx > 0.0) {
        overlay.orbitRadiusPx =
            std::max(1.0, solved.polarisPolarRadiusPx + input.radiusAdjustPx);
    }
    if (solved.hasDetectedPolarisPixel) {
        overlay.hasDetectedPolaris = true;
        overlay.detectedPolarisPosition = solved.detectedPolarisPixel;
        if (solved.hasPredictedPolarisPixel) {
            overlay.hasPredictedPolaris = true;
            overlay.predictedPolarisPosition = solved.predictedPolarisPixel;
        }
        overlay.hasStar = true;
        overlay.starPosition = solved.detectedPolarisPixel;
        overlay.deviationPx = 0.0;
        overlay.label = QStringLiteral("自动识别北极星: RMS %1 px")
                            .arg(solved.rmsPx, 0, 'f', 2);
    } else if (solved.hasPredictedPolarisPixel) {
        overlay.hasPredictedPolaris = true;
        overlay.predictedPolarisPosition = solved.predictedPolarisPixel;
        overlay.hasStar = true;
        overlay.starPosition = solved.predictedPolarisPixel;
        overlay.deviationPx = 0.0;
        overlay.label = QStringLiteral("预测北极星: RMS %1 px")
                            .arg(solved.rmsPx, 0, 'f', 2);
    }

    if (overlay.hasStar) {
        const QPointF delta = overlay.starPosition - overlay.orbitCenter;
        overlay.polarisNcpDistancePx = std::hypot(delta.x(), delta.y());
        overlay.polarisNcpDistanceArcmin =
            overlay.polarisNcpDistancePx * overlay.plateScaleArcsecPx / 60.0;
    }
    return overlay;
}

void AlignmentUiPresenter::applyConfirmedPolarisToOverlay(
    bool hasConfirmedPolarisPosition,
    const QPointF& confirmedPolarisPosition,
    FullFrameCanvas::AlignmentOverlay* overlay)
{
    if (!overlay) {
        return;
    }

    if (hasConfirmedPolarisPosition) {
        const QPointF orbitDelta = confirmedPolarisPosition - overlay->orbitCenter;
        const double orbitDistance = std::hypot(orbitDelta.x(), orbitDelta.y());
        overlay->hasStar = true;
        overlay->starPosition = confirmedPolarisPosition;
        overlay->deviationPx = std::abs(orbitDistance - overlay->orbitRadiusPx);
        overlay->label = QStringLiteral("偏离轨道: %1 px").arg(overlay->deviationPx, 0, 'f', 1);
    }

    if (!overlay->hasStar) {
        return;
    }

    const QPointF ncpDelta = overlay->starPosition - overlay->orbitCenter;
    overlay->polarisNcpDistancePx = std::hypot(ncpDelta.x(), ncpDelta.y());
    overlay->polarisNcpDistanceArcmin =
        overlay->polarisNcpDistancePx * overlay->plateScaleArcsecPx / 60.0;
}
