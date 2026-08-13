#pragma once

#include "InitialStarDetectionConfig.h"
#include "PolarisDetectionPipeline.h"

#include <QPointF>
#include <QVector>

#include <opencv2/core/mat.hpp>

struct RoiRect;

cv::Mat cropFrameForRoiProcessing(const cv::Mat& frame, const RoiRect& roi);
QVector<PolarisDetectionPipeline::InitialStarCandidate> detectInitialStarCandidates(const cv::Mat& grayscale,
                                                                                    const InitialStarDetectionConfig& config,
                                                                                    double* peakValue = nullptr,
                                                                                    double* thresholdValue = nullptr,
                                                                                    double* otsuThresholdValue = nullptr);
QVector<PolarisDetectionPipeline::InitialStarCandidate> detectInitialStarCandidates(const cv::Mat& grayscale,
                                                                                    double* peakValue = nullptr,
                                                                                    double* thresholdValue = nullptr,
                                                                                    double* otsuThresholdValue = nullptr);
bool detectRawInitialStarPeakCandidate(
    const cv::Mat& grayscale,
    PolarisDetectionPipeline::InitialStarCandidate* candidate,
    double* peakValue = nullptr);
bool detectInitialStarCentroid(const cv::Mat& grayscale, QPointF* centroid, double* peakValue);
bool detectInitialStarCentroidFast(const cv::Mat& grayscale, QPointF* centroid, double* peakValue);
