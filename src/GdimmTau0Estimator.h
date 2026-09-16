#pragma once

#include <cstddef>
#include <vector>

namespace GdimmTau0 {

struct TimedCentroids {
    std::vector<double> timestampsSeconds;
    std::vector<double> x;
    std::vector<double> y;
    double xWhiteNoiseVarianceRad2 = 0.0;
    double yWhiteNoiseVarianceRad2 = 0.0;
    bool hasWhiteNoiseEstimate = false;
};

struct AxisDecorrelation {
    double tauAaSeconds = 0.0;
    double sampleIntervalSeconds = 0.0;
    bool valid = false;
    bool underResolved = false;
    bool noiseCorrected = false;
};

struct ApertureDecorrelation {
    double diameterMeters = 0.0;
    AxisDecorrelation x;
    AxisDecorrelation y;
};

struct EstimateResult {
    double tau0Ms = 0.0;
    double tau0UpperBoundMs = 0.0;
    double effectiveWindSpeedMps = 0.0;
    double sampleIntervalMs = 0.0;
    double apertureDisagreementRatioX = 0.0;
    double apertureDisagreementRatioY = 0.0;
    std::size_t validApertureCount = 0;
    bool valid = false;
    bool underResolved = false;
    bool noiseCorrected = false;
};

AxisDecorrelation estimateAxisDecorrelation(
    const std::vector<double>& timestampsSeconds,
    const std::vector<double>& values,
    double whiteNoiseVarianceRad2,
    double maxLagSeconds,
    std::size_t minimumSamples);

double outerScaleFactorG(double apertureMeters, double outerScaleMeters);
double effectiveWindSpeedMps(double apertureMeters,
                             double outerScaleMeters,
                             double tauAaXSeconds,
                             double tauAaYSeconds);

EstimateResult combineApertures(
    const std::vector<ApertureDecorrelation>& apertures,
    double friedParameterMeters,
    double outerScaleMeters,
    double maxApertureDisagreementRatio = 0.30);

EstimateResult estimate(const std::vector<TimedCentroids>& apertures,
                        double friedParameterMeters,
                        double apertureDiameterMeters,
                        double outerScaleMeters,
                        double maxLagSeconds,
                        std::size_t minimumSamples,
                        double maxApertureDisagreementRatio = 0.30);

} // namespace GdimmTau0
