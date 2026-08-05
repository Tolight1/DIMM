#pragma once

#include "CanvasWidgets.h"
#include "PolarisSolver.h"

#include <QPointF>
#include <QRect>
#include <QString>
#include <QVector>

#include <limits>

namespace PolarisDetectionPipeline {
struct InitialStarCandidate {
    int index = 0;
    QPointF center;
    int area = 0;
    double peak = 0.0;
    double signal = 0.0;
    QRect bbox;
    double distanceToPreference = std::numeric_limits<double>::infinity();
};

struct InitialStarSelection {
    bool selected = false;
    InitialStarCandidate candidate;
    bool requiresUserSelection = false;
    QString reason;
};

InitialStarSelection selectInitialStarCandidate(QVector<InitialStarCandidate> candidates,
                                                bool hasPreference,
                                                const QPointF& preference,
                                                int selectedCandidateIndex);
bool chooseAutomaticInitialStarCandidate(const QVector<InitialStarCandidate>& candidates,
                                         const InitialStarCandidate& strongestCandidate,
                                         InitialStarCandidate* selected,
                                         QString* reason);
QVector<FullFrameCanvas::StarCandidateOverlay> buildCandidateOverlays(
    const QVector<InitialStarCandidate>& candidates,
    int selectedIndex);
QVector<InitialStarCandidate> initialCandidatesFromPolarisDetections(
    const QVector<DetectedStar>& detections);
}
