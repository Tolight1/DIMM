#include "GdimmTau0Estimator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace GdimmTau0 {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kE = 2.71828182845904523536;

bool isFinitePositive(double value)
{
    return std::isfinite(value) && value > 0.0;
}

double median(std::vector<double> values)
{
    if (values.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    return values.size() % 2 == 0
               ? 0.5 * (values[middle - 1] + values[middle])
               : values[middle];
}

bool isValidAxis(const AxisDecorrelation& axis)
{
    return axis.valid && isFinitePositive(axis.tauAaSeconds) &&
           isFinitePositive(axis.sampleIntervalSeconds);
}

double relativeDisagreement(double first, double second)
{
    const double average = 0.5 * (first + second);
    return isFinitePositive(average)
               ? std::abs(first - second) / average
               : std::numeric_limits<double>::quiet_NaN();
}

} // namespace

AxisDecorrelation estimateAxisDecorrelation(
    const std::vector<double>& timestampsSeconds,
    const std::vector<double>& values,
    double whiteNoiseVarianceRad2,
    double maxLagSeconds,
    std::size_t minimumSamples)
{
    if (timestampsSeconds.size() != values.size() ||
        timestampsSeconds.size() < minimumSamples || minimumSamples < 2 ||
        !isFinitePositive(maxLagSeconds) ||
        !std::isfinite(whiteNoiseVarianceRad2) || whiteNoiseVarianceRad2 < 0.0) {
        return {};
    }

    std::vector<double> intervals;
    intervals.reserve(timestampsSeconds.size() - 1);
    for (std::size_t index = 0; index < timestampsSeconds.size(); ++index) {
        if (!std::isfinite(timestampsSeconds[index]) || !std::isfinite(values[index])) {
            return {};
        }
        if (index > 0) {
            const double interval = timestampsSeconds[index] - timestampsSeconds[index - 1];
            if (!isFinitePositive(interval)) {
                return {};
            }
            intervals.push_back(interval);
        }
    }

    const double sampleIntervalSeconds = median(intervals);
    if (!isFinitePositive(sampleIntervalSeconds)) {
        return {};
    }

    std::vector<double> resampled;
    resampled.reserve(values.size());
    std::size_t sourceIndex = 0;
    const double firstTime = timestampsSeconds.front();
    const double lastTime = timestampsSeconds.back();
    for (std::size_t gridIndex = 0;; ++gridIndex) {
        const double gridTime = firstTime +
                                static_cast<double>(gridIndex) * sampleIntervalSeconds;
        if (gridTime > lastTime + sampleIntervalSeconds * 1e-9) {
            break;
        }
        while (sourceIndex + 1 < timestampsSeconds.size() &&
               timestampsSeconds[sourceIndex + 1] < gridTime) {
            ++sourceIndex;
        }
        if (sourceIndex + 1 == timestampsSeconds.size()) {
            if (std::abs(gridTime - lastTime) <= sampleIntervalSeconds * 1e-9) {
                resampled.push_back(values.back());
            }
            break;
        }

        const double t0 = timestampsSeconds[sourceIndex];
        const double t1 = timestampsSeconds[sourceIndex + 1];
        const double v0 = values[sourceIndex];
        const double v1 = values[sourceIndex + 1];
        const double weight = (gridTime - t0) / (t1 - t0);
        const double interpolated = v0 + weight * (v1 - v0);
        if (!std::isfinite(interpolated)) {
            return {};
        }
        resampled.push_back(interpolated);
    }

    if (resampled.size() < minimumSamples) {
        return {};
    }

    const double sampleCount = static_cast<double>(resampled.size());
    double meanTime = 0.0;
    double meanValue = 0.0;
    for (std::size_t index = 0; index < resampled.size(); ++index) {
        meanTime += static_cast<double>(index) * sampleIntervalSeconds;
        meanValue += resampled[index];
    }
    meanTime /= sampleCount;
    meanValue /= sampleCount;

    double slopeNumerator = 0.0;
    double slopeDenominator = 0.0;
    for (std::size_t index = 0; index < resampled.size(); ++index) {
        const double centeredTime =
            static_cast<double>(index) * sampleIntervalSeconds - meanTime;
        slopeNumerator += centeredTime * (resampled[index] - meanValue);
        slopeDenominator += centeredTime * centeredTime;
    }
    const double slope = slopeDenominator > 0.0
                             ? slopeNumerator / slopeDenominator
                             : 0.0;

    std::vector<double> detrended;
    detrended.reserve(resampled.size());
    double varianceSum = 0.0;
    for (std::size_t index = 0; index < resampled.size(); ++index) {
        const double centeredTime =
            static_cast<double>(index) * sampleIntervalSeconds - meanTime;
        const double value = resampled[index] - (meanValue + slope * centeredTime);
        if (!std::isfinite(value)) {
            return {};
        }
        detrended.push_back(value);
        varianceSum += value * value;
    }

    const double measuredVariance = varianceSum / sampleCount;
    const bool noiseCorrected = whiteNoiseVarianceRad2 > 0.0;
    if (!isFinitePositive(measuredVariance) ||
        (noiseCorrected && whiteNoiseVarianceRad2 >= measuredVariance)) {
        return {};
    }
    const double atmosphericVariance = measuredVariance -
                                       (noiseCorrected ? whiteNoiseVarianceRad2 : 0.0);
    if (!isFinitePositive(atmosphericVariance)) {
        return {};
    }

    const std::size_t maxLagByTime = static_cast<std::size_t>(std::ceil(
        maxLagSeconds / sampleIntervalSeconds));
    const std::size_t maxLag = std::min(detrended.size() / 2, maxLagByTime);
    if (maxLag < 1) {
        return {};
    }

    const double threshold = 2.0 * atmosphericVariance / kE;
    double previousStructure = 0.0;
    for (std::size_t lag = 1; lag <= maxLag; ++lag) {
        double sum = 0.0;
        for (std::size_t index = 0; index + lag < detrended.size(); ++index) {
            const double delta = detrended[index] - detrended[index + lag];
            sum += delta * delta;
        }
        const double measuredStructure =
            sum / static_cast<double>(detrended.size() - lag);
        const double structure = noiseCorrected
                                     ? std::max(0.0,
                                                measuredStructure -
                                                    2.0 * whiteNoiseVarianceRad2)
                                     : measuredStructure;
        if (!std::isfinite(structure)) {
            return {};
        }
        if (structure >= threshold) {
            AxisDecorrelation result;
            result.valid = true;
            result.sampleIntervalSeconds = sampleIntervalSeconds;
            result.noiseCorrected = noiseCorrected;
            if (lag == 1) {
                result.underResolved = true;
                result.tauAaSeconds = sampleIntervalSeconds;
                return result;
            }

            const double denominator = structure - previousStructure;
            if (!isFinitePositive(denominator)) {
                return {};
            }
            const double fraction = (threshold - previousStructure) / denominator;
            const double crossingLag = static_cast<double>(lag - 1) + fraction;
            if (!isFinitePositive(crossingLag)) {
                return {};
            }
            result.tauAaSeconds = crossingLag * sampleIntervalSeconds;
            return result;
        }
        previousStructure = structure;
    }

    return {};
}

double outerScaleFactorG(double apertureMeters, double outerScaleMeters)
{
    if (!isFinitePositive(apertureMeters) || !isFinitePositive(outerScaleMeters)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double k = kPi * apertureMeters / outerScaleMeters;
    const double g = 7.30 / kE * std::cbrt(k) + 7.01 * (1.0 - 1.0 / kE);
    return isFinitePositive(g) ? g : std::numeric_limits<double>::quiet_NaN();
}

double effectiveWindSpeedMps(double apertureMeters,
                             double outerScaleMeters,
                             double tauAaXSeconds,
                             double tauAaYSeconds)
{
    const double g = outerScaleFactorG(apertureMeters, outerScaleMeters);
    if (!isFinitePositive(g) || !isFinitePositive(tauAaXSeconds) ||
        !isFinitePositive(tauAaYSeconds)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double tauSum = std::cbrt(tauAaXSeconds) + std::cbrt(tauAaYSeconds);
    const double vEff = 1000.0 * apertureMeters /
                        (g * g * g) /
                        (tauSum * tauSum * tauSum);
    return isFinitePositive(vEff) ? vEff : std::numeric_limits<double>::quiet_NaN();
}

EstimateResult combineApertures(
    const std::vector<ApertureDecorrelation>& apertures,
    double friedParameterMeters,
    double outerScaleMeters,
    double maxApertureDisagreementRatio)
{
    EstimateResult result;
    if (apertures.size() != 2 || !isFinitePositive(friedParameterMeters) ||
        !isFinitePositive(outerScaleMeters) ||
        !std::isfinite(maxApertureDisagreementRatio) ||
        maxApertureDisagreementRatio < 0.0 ||
        !isFinitePositive(apertures[0].diameterMeters) ||
        !isFinitePositive(apertures[1].diameterMeters) ||
        !isValidAxis(apertures[0].x) || !isValidAxis(apertures[0].y) ||
        !isValidAxis(apertures[1].x) || !isValidAxis(apertures[1].y)) {
        return result;
    }

    result.apertureDisagreementRatioX = relativeDisagreement(
        apertures[0].x.tauAaSeconds, apertures[1].x.tauAaSeconds);
    result.apertureDisagreementRatioY = relativeDisagreement(
        apertures[0].y.tauAaSeconds, apertures[1].y.tauAaSeconds);
    if (!std::isfinite(result.apertureDisagreementRatioX) ||
        !std::isfinite(result.apertureDisagreementRatioY) ||
        result.apertureDisagreementRatioX > maxApertureDisagreementRatio ||
        result.apertureDisagreementRatioY > maxApertureDisagreementRatio) {
        return result;
    }

    const double apertureMeters = 0.5 *
                                  (apertures[0].diameterMeters +
                                   apertures[1].diameterMeters);
    const double tauAaXSeconds = 0.5 *
                                 (apertures[0].x.tauAaSeconds +
                                  apertures[1].x.tauAaSeconds);
    const double tauAaYSeconds = 0.5 *
                                 (apertures[0].y.tauAaSeconds +
                                  apertures[1].y.tauAaSeconds);
    result.effectiveWindSpeedMps = effectiveWindSpeedMps(apertureMeters,
                                                         outerScaleMeters,
                                                         tauAaXSeconds,
                                                         tauAaYSeconds);
    if (!isFinitePositive(result.effectiveWindSpeedMps)) {
        return {};
    }

    result.tau0Ms = 1000.0 * 0.31 * friedParameterMeters /
                    result.effectiveWindSpeedMps;
    if (!isFinitePositive(result.tau0Ms)) {
        return {};
    }

    result.sampleIntervalMs = 1000.0 * std::max(
        std::max(apertures[0].x.sampleIntervalSeconds,
                 apertures[0].y.sampleIntervalSeconds),
        std::max(apertures[1].x.sampleIntervalSeconds,
                 apertures[1].y.sampleIntervalSeconds));
    result.validApertureCount = 2;
    result.underResolved = apertures[0].x.underResolved ||
                           apertures[0].y.underResolved ||
                           apertures[1].x.underResolved ||
                           apertures[1].y.underResolved;
    result.noiseCorrected = apertures[0].x.noiseCorrected &&
                            apertures[0].y.noiseCorrected &&
                            apertures[1].x.noiseCorrected &&
                            apertures[1].y.noiseCorrected;
    result.tau0UpperBoundMs = result.underResolved ? result.tau0Ms : 0.0;
    result.valid = true;
    return result;
}

EstimateResult estimate(const std::vector<TimedCentroids>& apertures,
                        double friedParameterMeters,
                        double apertureDiameterMeters,
                        double outerScaleMeters,
                        double maxLagSeconds,
                        std::size_t minimumSamples,
                        double maxApertureDisagreementRatio)
{
    if (apertures.size() != 2 || !isFinitePositive(apertureDiameterMeters)) {
        return {};
    }

    std::vector<ApertureDecorrelation> decorrelations;
    decorrelations.reserve(apertures.size());
    for (const TimedCentroids& aperture : apertures) {
        const double xNoise = aperture.hasWhiteNoiseEstimate
                                  ? aperture.xWhiteNoiseVarianceRad2
                                  : 0.0;
        const double yNoise = aperture.hasWhiteNoiseEstimate
                                  ? aperture.yWhiteNoiseVarianceRad2
                                  : 0.0;
        ApertureDecorrelation result;
        result.diameterMeters = apertureDiameterMeters;
        result.x = estimateAxisDecorrelation(aperture.timestampsSeconds,
                                             aperture.x,
                                             xNoise,
                                             maxLagSeconds,
                                             minimumSamples);
        result.y = estimateAxisDecorrelation(aperture.timestampsSeconds,
                                             aperture.y,
                                             yNoise,
                                             maxLagSeconds,
                                             minimumSamples);
        decorrelations.push_back(result);
    }

    return combineApertures(decorrelations,
                            friedParameterMeters,
                            outerScaleMeters,
                            maxApertureDisagreementRatio);
}

} // namespace GdimmTau0
