#include "AlignmentCoarseController.h"

#include "FullFrameStarDetector.h"
#include "ImageUtils.h"

#include <QElapsedTimer>
#include <QMetaObject>

#include <utility>

using PolarisDetectionPipeline::InitialStarCandidate;

class AlignmentCoarseWorker : public QObject {
    Q_OBJECT

public:
    struct PendingFrame {
        bool valid = false;
        int cameraIndex = 0;
        cv::Mat frame;
        InitialStarDetectionConfig starConfig;
        CoarseAlignmentConfig coarseConfig;
        quint64 generation = 0;
        quint64 frameId = 0;
        qint64 timestampMs = 0;
    };

    void submit(PendingFrame task);
    void resetCamera(int cameraIndex);
    void resetAll();
    void cancelAll(quint64 generation);

signals:
    void estimateReady(CoarseAlignmentEstimate estimate);

private:
    void startCoarseTask(const PendingFrame& task);
    CoarseAlignmentEstimate processTask(const PendingFrame& task);

    quint64 m_generation = 0;
    bool m_taskRunning[2] = {false, false};
    PendingFrame m_pendingLatest[2];
    AlignmentCoarseTracker m_trackers[2];
};

void AlignmentCoarseWorker::submit(PendingFrame task)
{
    if (task.cameraIndex < 0 || task.cameraIndex >= 2) {
        return;
    }
    if (m_taskRunning[task.cameraIndex]) {
        m_pendingLatest[task.cameraIndex] = std::move(task);
        return;
    }
    startCoarseTask(task);
}

void AlignmentCoarseWorker::startCoarseTask(const PendingFrame& task)
{
    if (task.cameraIndex < 0 || task.cameraIndex >= 2) {
        return;
    }
    m_taskRunning[task.cameraIndex] = true;
    const CoarseAlignmentEstimate estimate = processTask(task);
    m_taskRunning[task.cameraIndex] = false;
    emit estimateReady(estimate);

    if (m_pendingLatest[task.cameraIndex].valid) {
        PendingFrame next = std::move(m_pendingLatest[task.cameraIndex]);
        m_pendingLatest[task.cameraIndex] = PendingFrame();
        startCoarseTask(next);
    }
}

CoarseAlignmentEstimate AlignmentCoarseWorker::processTask(const PendingFrame& task)
{
    CoarseAlignmentEstimate estimate;
    if (task.cameraIndex < 0 || task.cameraIndex >= 2) {
        return estimate;
    }
    estimate.cameraIndex = task.cameraIndex;
    estimate.generation = task.generation;
    estimate.frameId = task.frameId;

    const cv::Mat grayscale = ImageUtils::grayscaleDetectionFrame(task.frame);
    if (grayscale.empty()) {
        return estimate;
    }

    QElapsedTimer timer;
    timer.start();

    double peakValue = 0.0;
    double thresholdValue = 0.0;
    QVector<InitialStarCandidate> candidates =
        detectInitialStarCandidates(grayscale, task.starConfig, &peakValue, &thresholdValue);

    estimate = m_trackers[task.cameraIndex].addFrame(task.cameraIndex,
                                                     task.generation,
                                                     task.frameId,
                                                     task.timestampMs,
                                                     QSize(grayscale.cols, grayscale.rows),
                                                     std::move(candidates),
                                                     task.coarseConfig);

    estimate.processingMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    return estimate;
}

void AlignmentCoarseWorker::resetCamera(int cameraIndex)
{
    if (cameraIndex < 0 || cameraIndex >= 2) {
        return;
    }
    m_pendingLatest[cameraIndex] = PendingFrame();
    m_trackers[cameraIndex].reset();
}

void AlignmentCoarseWorker::resetAll()
{
    for (int i = 0; i < 2; ++i) {
        m_pendingLatest[i] = PendingFrame();
        m_trackers[i].reset();
    }
}

void AlignmentCoarseWorker::cancelAll(quint64 generation)
{
    m_generation = generation;
    for (int i = 0; i < 2; ++i) {
        m_pendingLatest[i] = PendingFrame();
        m_trackers[i].reset();
    }
}

AlignmentCoarseController::AlignmentCoarseController(QObject* parent)
    : QObject(parent)
{
    m_workerThread = new QThread(this);
    m_workerThread->setObjectName(QStringLiteral("AlignmentCoarseWorkerThread"));
    m_worker = new AlignmentCoarseWorker();
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &AlignmentCoarseWorker::estimateReady,
            this, &AlignmentCoarseController::estimateReady);

    m_workerThread->start();
}

AlignmentCoarseController::~AlignmentCoarseController()
{
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void AlignmentCoarseController::submitFrame(int cameraIndex,
                                            const cv::Mat& frame,
                                            const InitialStarDetectionConfig& starConfig,
                                            const CoarseAlignmentConfig& coarseConfig,
                                            quint64 generation,
                                            quint64 frameId,
                                            qint64 timestampMs)
{
    if (!m_worker || !m_workerThread) {
        return;
    }

    AlignmentCoarseWorker::PendingFrame task;
    task.valid = true;
    task.cameraIndex = cameraIndex;
    task.frame = frame;
    task.starConfig = starConfig;
    task.coarseConfig = coarseConfig;
    task.generation = generation;
    task.frameId = frameId;
    task.timestampMs = timestampMs;

    QMetaObject::invokeMethod(m_worker,
                              [worker = m_worker, task]() mutable {
                                  worker->submit(std::move(task));
                              },
                              Qt::QueuedConnection);
}

void AlignmentCoarseController::resetCamera(int cameraIndex)
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker,
                              [worker = m_worker, cameraIndex]() {
                                  worker->resetCamera(cameraIndex);
                              },
                              Qt::QueuedConnection);
}

void AlignmentCoarseController::resetAll()
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker,
                              [worker = m_worker]() {
                                  worker->resetAll();
                              },
                              Qt::QueuedConnection);
}

void AlignmentCoarseController::cancelAll(quint64 generation)
{
    if (!m_worker) {
        return;
    }
    QMetaObject::invokeMethod(m_worker,
                              [worker = m_worker, generation]() {
                                  worker->cancelAll(generation);
                              },
                              Qt::QueuedConnection);
}

#include "AlignmentCoarseController.moc"
