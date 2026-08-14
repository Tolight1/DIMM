#pragma once

#include "ConnectedDomain.h"

struct InitialStarDetectionConfig {
    double sigmaThreshold = 4.0;
    double peakFraction = 0.20;
    int minArea = ConnectedDomain::kMinimumComponentArea;
    int maxArea = 1000;
    int connectivity = ConnectedDomain::kDefaultConnectivity;
};

InitialStarDetectionConfig currentInitialStarDetectionConfig();
void setCurrentInitialStarDetectionConfig(const InitialStarDetectionConfig& config);
