#pragma once

#include <functional>

#include <QPointF>
#include <QRect>

#include <opencv2/opencv.hpp>

#include "AlignmentTypes.h"

namespace AlignmentLocalTracker {
using CentroidDetector = std::function<bool(const cv::Mat&, QPointF*, double*)>;

bool trackInWindow(const cv::Mat& frame,
                   const QRect& trackingWindow,
                   const CentroidDetector& centroidDetector,
                   QPointF* trackedPosition,
                   double* peakValue);

bool trackFromConfirmedPosition(const cv::Mat& frame,
                                const QPointF& confirmedPosition,
                                AlignmentCameraSolveRuntime* runtime,
                                const CentroidDetector& centroidDetector,
                                QPointF* trackedPosition,
                                double* peakValue);
}
