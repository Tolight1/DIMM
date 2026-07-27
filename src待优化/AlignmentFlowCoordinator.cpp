#include "AlignmentFlowCoordinator.h"

AlignmentFlowCoordinator::ToggleAction AlignmentFlowCoordinator::actionForToggle(bool alignmentActive)
{
    return alignmentActive ? ToggleAction::Stop : ToggleAction::Start;
}

AlignmentFlowCoordinator::StartReadiness AlignmentFlowCoordinator::validateStartReadiness(
    bool hasCameraManager,
    bool alignmentActive,
    bool idleOrPaused,
    bool paused,
    int openCameraCount)
{
    StartReadiness readiness;
    if (!hasCameraManager) {
        readiness.reason = QStringLiteral("相机管理器未初始化。");
        return readiness;
    }

    if (alignmentActive) {
        readiness.canStart = true;
        readiness.alreadyActive = true;
        return readiness;
    }

    if (!idleOrPaused) {
        readiness.reason = QStringLiteral("请先停止当前采集或模拟采集，再进入对准模式。");
        return readiness;
    }

    if (openCameraCount < 2) {
        readiness.reason = QStringLiteral("对准模式需要两台相机均已连接。");
        return readiness;
    }

    readiness.canStart = true;
    readiness.shouldStopPausedCapture = paused;
    return readiness;
}

AlignmentSolveState AlignmentFlowCoordinator::initialSolveState(bool autoSolveEnabled)
{
    return autoSolveEnabled ? AlignmentSolveState::WaitingFrame : AlignmentSolveState::Disabled;
}

QString AlignmentFlowCoordinator::waitingLabelText()
{
    return QStringLiteral("自动识别: 等待全画幅");
}

QString AlignmentFlowCoordinator::stoppedLabelText()
{
    return QStringLiteral("自动识别: 未启动");
}

QString AlignmentFlowCoordinator::startedStatusText(double previewRateHz)
{
    return QStringLiteral("状态: 对准模式已启动，双相机 %1 Hz 全画幅预览")
        .arg(previewRateHz, 0, 'f', 1);
}

QString AlignmentFlowCoordinator::stoppedStatusText()
{
    return QStringLiteral("状态: 已退出对准模式");
}
