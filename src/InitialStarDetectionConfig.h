#pragma once

struct InitialStarDetectionConfig {
    double thresholdAbsolute = -1.0;
    double sigmaThreshold = 4.0;
    double peakFraction = 0.20;
    double minimumIntensity = 16.0;
    int minArea = 8;
    int maxArea = 1000;
};

InitialStarDetectionConfig currentInitialStarDetectionConfig();
void setCurrentInitialStarDetectionConfig(const InitialStarDetectionConfig& config);
