#include "AlignmentLocalTracker.h"

#include "PolarisTracker.h"

bool AlignmentLocalTracker::trackInWindow(const cv::Mat& frame,
                                          const QRect& trackingWindow,
                                          const CentroidDetector& centroidDetector,
                                          QPointF* trackedPosition,
                                          double* peakValue)
{
    if (frame.empty() ||
        trackingWindow.isEmpty() ||
        !centroidDetector ||
        !trackedPosition) {
        return false;
    }

    cv::Mat grayscale;
    if (frame.channels() == 1) {
        grayscale = frame;
    } else {
        cv::cvtColor(frame, grayscale, cv::COLOR_BGR2GRAY);
    }

    const cv::Rect roi(trackingWindow.x(),
                       trackingWindow.y(),
                       trackingWindow.width(),
                       trackingWindow.height());
    QPointF localCentroid;
    double localPeak = 0.0;
    if (!centroidDetector(grayscale(roi), &localCentroid, &localPeak)) {
        return false;
    }

    *trackedPosition = QPointF(localCentroid.x() + trackingWindow.x(),
                               localCentroid.y() + trackingWindow.y());
    if (peakValue) {
        *peakValue = localPeak;
    }
    return true;
}

bool AlignmentLocalTracker::trackFromConfirmedPosition(const cv::Mat& frame,
                                                       const QPointF& confirmedPosition,
                                                       AlignmentCameraSolveRuntime* runtime,
                                                       const CentroidDetector& centroidDetector,
                                                       QPointF* trackedPosition,
                                                       double* peakValue)
{
    if (frame.empty() || !runtime || !trackedPosition) {
        return false;
    }

    const QRect trackingWindow =
        PolarisTracker::trackingWindowForPosition(confirmedPosition, QSize(frame.cols, frame.rows));
    runtime->trackingWindow = trackingWindow;
    if (!PolarisTracker::isUsableTrackingWindow(trackingWindow)) {
        return false;
    }

    return trackInWindow(frame, trackingWindow, centroidDetector, trackedPosition, peakValue);
}
