#include "AutoFocusMetricCalculator.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

bool finitePositive(double value)
{
    return std::isfinite(value) && value > 0.0;
}

} // namespace

AutoFocusSample AutoFocusMetricCalculator::calculate(const AutoFocusRoiMeasurement& measurement)
{
    AutoFocusSample sample;
    if (!measurement.centroidValid || measurement.calculationImage.empty() ||
        measurement.calculationImage.channels() != 1 ||
        !std::isfinite(measurement.centroidX) || !std::isfinite(measurement.centroidY) ||
        measurement.centroidX < 0.0 || measurement.centroidY < 0.0 ||
        measurement.centroidX >= measurement.calculationImage.cols ||
        measurement.centroidY >= measurement.calculationImage.rows) {
        return sample;
    }

    cv::Mat intensity;
    measurement.calculationImage.convertTo(intensity, CV_64FC1);
    if (intensity.empty()) {
        return sample;
    }

    double totalFlux = 0.0;
    double weightedRadiusSquared = 0.0;
    std::vector<std::pair<double, double>> radialFlux;
    radialFlux.reserve(static_cast<std::size_t>(intensity.total()));
    for (int y = 0; y < intensity.rows; ++y) {
        const double* intensityRow = intensity.ptr<double>(y);
        for (int x = 0; x < intensity.cols; ++x) {
            // The centroid stage has already subtracted the background
            // threshold. HFR/RMS use every finite positive calculation pixel.
            const double weight = intensityRow[x];
            if (finitePositive(weight)) {
                const double dx = static_cast<double>(x) - measurement.centroidX;
                const double dy = static_cast<double>(y) - measurement.centroidY;
                const double radiusSquared = dx * dx + dy * dy;
                totalFlux += weight;
                weightedRadiusSquared += weight * radiusSquared;
                radialFlux.emplace_back(radiusSquared, weight);
            }
        }
    }
    // A positive finite total is required only to keep the divisions below
    // defined; there is no connected-component, area, or sample-count gate.
    if (!finitePositive(totalFlux)) {
        return sample;
    }

    std::sort(radialFlux.begin(), radialFlux.end());
    const double halfFlux = totalFlux / 2.0;
    double accumulatedFlux = 0.0;
    double halfFluxRadius = 0.0;
    for (const auto& [radiusSquared, flux] : radialFlux) {
        accumulatedFlux += flux;
        if (accumulatedFlux >= halfFlux) {
            halfFluxRadius = std::sqrt(radiusSquared);
            break;
        }
    }

    sample.hfr = halfFluxRadius;
    sample.rms = std::sqrt(weightedRadiusSquared / totalFlux);
    sample.valid = finitePositive(sample.hfr) && finitePositive(sample.rms);
    return sample;
}
