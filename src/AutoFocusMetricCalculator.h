#pragma once

#include "AutoFocusController.h"

#include <opencv2/core.hpp>

struct AutoFocusRoiMeasurement {
    // The same background-subtracted calculation image produced by the
    // centroid stage.
    cv::Mat calculationImage;
    bool centroidValid = false;
    double centroidX = 0.0;
    double centroidY = 0.0;
};

class AutoFocusMetricCalculator {
public:
    static AutoFocusSample calculate(const AutoFocusRoiMeasurement& measurement);
};
