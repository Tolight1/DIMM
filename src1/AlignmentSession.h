#pragma once

#include "AlignmentTypes.h"
#include "PolarisDetectionPipeline.h"

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <opencv2/opencv.hpp>

struct AlignmentLiveRuntimeAccess {
    QPointF* confirmedPolarisPosition = nullptr;
    bool* hasConfirmedPolarisPosition = nullptr;
    QPointF* lastTargetPosition = nullptr;
    bool* hasLastTargetPosition = nullptr;
    int* selectedInitialCandidateIndex = nullptr;
    bool* pendingInitialCandidateSelectionRequired = nullptr;
    qint64* lastInitialCandidatePromptMs = nullptr;
};

struct AlignmentCandidateRuntimeAccess {
    QPointF* confirmedPolarisPosition = nullptr;
    bool* hasConfirmedPolarisPosition = nullptr;
    QPointF* lastTargetPosition = nullptr;
    bool* hasLastTargetPosition = nullptr;
    int* selectedInitialCandidateIndex = nullptr;
    bool* pendingInitialCandidateSelectionRequired = nullptr;
    bool* selectionRequested = nullptr;
    qint64* lastInitialCandidatePromptMs = nullptr;
};

struct AlignmentCandidateCollectionInput {
    using InitialStarCandidate = PolarisDetectionPipeline::InitialStarCandidate;
    using CandidateDetector =
        std::function<QVector<InitialStarCandidate>(const cv::Mat&, double*)>;

    int cameraIndex = -1;
    const cv::Mat* frame = nullptr;
    const PolarisSolveResult* solved = nullptr;
    bool autoSolveEnabled = false;
    bool hasCurrentSolverResult = false;
    quint64 lastFrameId = 0;
    bool allowGuiCandidateDetection = false;
    CandidateDetector candidateDetector;
};

class AlignmentSession final {
public:
    using InitialStarCandidate = PolarisDetectionPipeline::InitialStarCandidate;
    using InitialStarSelection = PolarisDetectionPipeline::InitialStarSelection;
    using CentroidDetector =
        std::function<bool(const cv::Mat&, QPointF*, double*)>;

    AlignmentSessionState& state();
    const AlignmentSessionState& state() const;

    AlignmentCameraFrameState& camera(int cameraIndex);
    const AlignmentCameraFrameState& camera(int cameraIndex) const;

    quint64 solveGeneration() const;
    quint64 advanceSolveGeneration();
    void resetForStart(bool autoSolveEnabled);
    void resetForStop();
    void resetCameraForStart(int cameraIndex,
                             AlignmentLiveRuntimeAccess runtime,
                             bool autoSolveEnabled);
    void resetCameraForStop(int cameraIndex);
    void applyManualConfirmation(int cameraIndex, const QPointF& selectedPosition);

    static QVector<InitialStarCandidate> collectCandidates(
        const AlignmentCandidateCollectionInput& input,
        cv::Mat* mono8,
        double* peakValue);
    static InitialStarSelection selectInitialCandidate(
        const AlignmentCandidateRuntimeAccess& runtime,
        const QVector<InitialStarCandidate>& candidates,
        bool manualSelectionRequested,
        QPointF* preferredTarget);
    static void recordSelectedCandidate(AlignmentCandidateRuntimeAccess runtime,
                                        const QPointF& star,
                                        int selectedCandidateIndex);
    static void updateFromFallbackCentroid(AlignmentCandidateRuntimeAccess runtime,
                                           const cv::Mat& frame,
                                           bool allowGuiCandidateDetection,
                                           cv::Mat* mono8,
                                           QPointF* star,
                                           double* peakValue,
                                           const CentroidDetector& centroidDetector);
    static bool canApplyCandidateSelection(bool manualSelectionRequested,
                                           bool hadConfirmedPolarisBeforeSelection);
    static bool shouldShowCandidatePrompt(qint64 lastPromptMs,
                                          qint64 nowMs,
                                          qint64 promptIntervalMs = 2000);
    static void recordCandidatePromptCancelled(qint64* lastPromptMs, qint64 nowMs);
    static void recordCandidatePromptAccepted(int* selectedCandidateIndex,
                                              qint64* lastPromptMs,
                                              int chosenCandidateIndex);
    static QString manualConfirmedMessage(const QPointF& selectedPosition);
    static QStringList candidatePromptLines(const QVector<InitialStarCandidate>& candidates);

private:
    AlignmentSessionState m_state;
};
