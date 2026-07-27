#pragma once

#include "AlignmentTypes.h"

#include <QString>

namespace AlignmentFlowCoordinator {
enum class ToggleAction {
    Start,
    Stop
};

struct StartReadiness {
    bool canStart = false;
    bool alreadyActive = false;
    bool shouldStopPausedCapture = false;
    QString reason;
};

ToggleAction actionForToggle(bool alignmentActive);
StartReadiness validateStartReadiness(bool hasCameraManager,
                                      bool alignmentActive,
                                      bool idleOrPaused,
                                      bool paused,
                                      int openCameraCount);
AlignmentSolveState initialSolveState(bool autoSolveEnabled);
QString waitingLabelText();
QString stoppedLabelText();
QString startedStatusText(double previewRateHz);
QString stoppedStatusText();
}
