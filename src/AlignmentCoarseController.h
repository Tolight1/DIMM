#pragma once

#include "AlignmentCoarseEstimator.h"
#include "InitialStarDetectionConfig.h"

#include <QObject>
#include <QThread>

#include <opencv2/core/mat.hpp>

class AlignmentCoarseWorker;

class AlignmentCoarseController : public QObject {
    Q_OBJECT

public:
    explicit AlignmentCoarseController(QObject* parent = nullptr);
    ~AlignmentCoarseController() override;

    void submitFrame(int cameraIndex,
                     const cv::Mat& frame,
                     const InitialStarDetectionConfig& starConfig,
                     const CoarseAlignmentConfig& coarseConfig,
                     quint64 generation,
                     quint64 frameId,
                     qint64 timestampMs);
    void resetCamera(int cameraIndex);
    void resetAll();
    void cancelAll(quint64 generation);

signals:
    void estimateReady(CoarseAlignmentEstimate estimate);

private:
    QThread* m_workerThread = nullptr;
    AlignmentCoarseWorker* m_worker = nullptr;
};
