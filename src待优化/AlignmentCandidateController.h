#pragma once

#include "AlignmentTypes.h"
#include "PolarisDetectionPipeline.h"
#include "PolarisSolver.h"

#include <QPointF>
#include <QVector>

#include <functional>
#include <opencv2/opencv.hpp>

namespace AlignmentCandidateController {
using InitialStarCandidate = PolarisDetectionPipeline::InitialStarCandidate;
using InitialStarSelection = PolarisDetectionPipeline::InitialStarSelection;
using CandidateDetector =
    std::function<QVector<InitialStarCandidate>(const cv::Mat&, double*)>;
using CentroidDetector =
    std::function<bool(const cv::Mat&, QPointF*, double*)>;

struct RuntimeAccess {
    QPointF* confirmedPolarisPosition = nullptr;
    bool* hasConfirmedPolarisPosition = nullptr;
    QPointF* lastTargetPosition = nullptr;
    bool* hasLastTargetPosition = nullptr;
    int* selectedInitialCandidateIndex = nullptr;
    bool* pendingInitialCandidateSelectionRequired = nullptr;
    bool* selectionRequested = nullptr;
    qint64* lastInitialCandidatePromptMs = nullptr;
};

struct CandidateCollectionInput {
    int cameraIndex = -1;
    const cv::Mat* frame = nullptr;
    const PolarisSolveResult* solved = nullptr;
    bool autoSolveEnabled = false;
    bool hasCurrentSolverResult = false;
    quint64 lastFrameId = 0;
    bool allowGuiCandidateDetection = false;
    CandidateDetector candidateDetector;
};

QVector<InitialStarCandidate> collectCandidates(const CandidateCollectionInput& input,
                                                cv::Mat* mono8,
                                                double* peakValue);

InitialStarSelection selectInitialCandidate(const RuntimeAccess& runtime,
                                            const QVector<InitialStarCandidate>& candidates,
                                            bool manualSelectionRequested,
                                            QPointF* preferredTarget);

void recordSelectedCandidate(RuntimeAccess runtime,
                             const QPointF& star,
                             int selectedCandidateIndex);

void updateFromFallbackCentroid(RuntimeAccess runtime,
                                const cv::Mat& frame,
                                bool allowGuiCandidateDetection,
                                cv::Mat* mono8,
                                QPointF* star,
                                double* peakValue,
                                const CentroidDetector& centroidDetector);
}
