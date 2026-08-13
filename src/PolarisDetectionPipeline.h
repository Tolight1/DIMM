#pragma once

#include "CanvasWidgets.h"
#include "PolarisSolver.h"

#include <QPointF>
#include <QRect>
#include <QString>
#include <QVector>

namespace PolarisDetectionPipeline {
struct InitialStarCandidate {
    int index = 0;
    QPointF center;
    int area = 0;
    double peak = 0.0;
    double signal = 0.0;
    QRect bbox;
};

struct InitialStarSelection {
    bool selected = false;
    InitialStarCandidate candidate;
    bool requiresUserSelection = false;
    QString reason;
};

InitialStarSelection selectFullFrameStarCandidate(
    const QVector<InitialStarCandidate>& candidates,
    int selectedCandidateIndex,
    bool manualSelectionConfirmed);
QVector<FullFrameCanvas::StarCandidateOverlay> buildCandidateOverlays(
    const QVector<InitialStarCandidate>& candidates,
    int selectedIndex);
QVector<InitialStarCandidate> initialCandidatesFromPolarisDetections(
    const QVector<DetectedStar>& detections);
}
