#include "CdimPsdAnalysis.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kEpsilon = 1.0e-18;

bool finite(double value)
{
    return std::isfinite(value);
}

double median(QVector<double> values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const int middle = values.size() / 2;
    if (values.size() % 2 == 0) {
        return 0.5 * (values.at(middle - 1) + values.at(middle));
    }
    return values.at(middle);
}

double nextPowerOfTwo(double value)
{
    int result = 1;
    const int target = std::max(1, static_cast<int>(std::ceil(value)));
    while (result < target && result < (1 << 30)) {
        result <<= 1;
    }
    return static_cast<double>(result);
}

void warning(CdimPsdAnalysisResult& result, const QString& message)
{
    if (!result.warnings.contains(message)) {
        result.warnings.append(message);
    }
}

struct SamplingInfo {
    double fsHz = 0.0;
    double relativeJitter = 0.0;
    CdimPsdFsSource source = CdimPsdFsSource::ConfiguredFallback;
    bool timestampAvailable = false;
    bool timingJitterAcceptable = true;
};

SamplingInfo inferSampling(const CdimPsdInput& input,
                           const CdimPsdAnalysisConfig& config,
                           int sampleCount,
                           const QVector<quint64>& timestamps,
                           const QVector<quint64>& frameIds,
                           CdimPsdAnalysisResult& result)
{
    SamplingInfo info;
    QVector<double> timestampIntervalsSec;
    for (int i = 1; i < sampleCount; ++i) {
        const quint64 previous = timestamps.value(i - 1, 0);
        const quint64 current = timestamps.value(i, 0);
        if (previous == 0 || current <= previous) {
            continue;
        }
        const double intervalSec = static_cast<double>(current - previous)
                                   * config.timestampTickUs * 1.0e-6;
        if (finite(intervalSec) && intervalSec > 0.0) {
            timestampIntervalsSec.append(intervalSec);
        }
    }

    const double medianIntervalSec = median(timestampIntervalsSec);
    if (timestampIntervalsSec.size() >= 2 && medianIntervalSec > 0.0) {
        info.fsHz = 1.0 / medianIntervalSec;
        info.source = CdimPsdFsSource::Timestamp;
        info.timestampAvailable = true;

        QVector<double> absoluteDeviations;
        absoluteDeviations.reserve(timestampIntervalsSec.size());
        for (double intervalSec : timestampIntervalsSec) {
            absoluteDeviations.append(std::abs(intervalSec - medianIntervalSec));
        }
        info.relativeJitter = median(absoluteDeviations) / medianIntervalSec;
        if (info.relativeJitter > config.maxRelativeTimingJitter) {
            info.timingJitterAcceptable = false;
            warning(result, QStringLiteral("Timestamp interval jitter exceeds the configured QC limit"));
        }
    } else {
        info.fsHz = input.configuredFrameRateHz;
        info.source = CdimPsdFsSource::ConfiguredFallback;
        if (finite(info.fsHz) && info.fsHz > 0.0) {
            warning(result, QStringLiteral("Timestamp unavailable; configured frame rate used for PSD"));
        } else {
            info.fsHz = 0.0;
            warning(result, QStringLiteral("Neither timestamp intervals nor configured frame rate is usable"));
        }
    }

    for (int i = 1; i < frameIds.size(); ++i) {
        const quint64 previous = frameIds.at(i - 1);
        const quint64 current = frameIds.at(i);
        if (previous > 0 && current > previous + 1) {
            warning(result, QStringLiteral("Frame gaps detected in the PSD history"));
            break;
        }
    }
    return info;
}

struct PsdData {
    QVector<double> frequencyHz;
    QVector<double> psd;
    double frequencyResolutionHz = 0.0;
};

PsdData calculatePsd(const QVector<double>& values,
                     double fsHz,
                     const CdimPsdAnalysisConfig& config,
                     CdimPsdAnalysisResult& result)
{
    PsdData output;
    if (values.size() < 2 || !finite(fsHz) || fsHz <= 0.0) {
        return output;
    }

    const int valueCount = static_cast<int>(values.size());
    int segmentLength = valueCount;
    int segmentStep = segmentLength;
    int nfft = valueCount;
    QVector<double> window;
    QVector<double> windowSquared;

    if (config.psdMode == CdimPsdMode::Welch) {
        segmentLength = std::clamp(config.welchSegmentLength, 2, valueCount);
        if (segmentLength < config.welchSegmentLength) {
            warning(result, QStringLiteral("Welch segment length was clipped to the available history"));
        }
        const double boundedOverlap = std::clamp(config.welchOverlap, 0.0, 0.95);
        segmentStep = std::max(1, static_cast<int>(std::llround(
            segmentLength * (1.0 - boundedOverlap))));
        nfft = config.nfft > 0
                   ? std::max(config.nfft, segmentLength)
                   : static_cast<int>(nextPowerOfTwo(segmentLength));
    } else {
        nfft = config.nfft > 0 ? std::max(config.nfft, valueCount) : valueCount;
    }

    window.resize(segmentLength);
    windowSquared.resize(segmentLength);
    for (int i = 0; i < segmentLength; ++i) {
        const double w = segmentLength == 1
                             ? 1.0
                             : 0.5 * (1.0 - std::cos(2.0 * kPi * i / (segmentLength - 1)));
        window[i] = w;
        windowSquared[i] = w * w;
    }
    const double windowEnergy = std::max(kEpsilon,
                                         std::accumulate(windowSquared.cbegin(),
                                                         windowSquared.cend(),
                                                         0.0));

    QVector<double> accumulated(nfft / 2 + 1, 0.0);
    int segmentCount = 0;
    for (int start = 0; start + segmentLength <= valueCount; start += segmentStep) {
        cv::Mat segment(nfft, 1, CV_64F, cv::Scalar(0));
        for (int i = 0; i < segmentLength; ++i) {
            segment.at<double>(i, 0) = values.at(start + i) * window.at(i);
        }
        cv::Mat spectrum;
        cv::dft(segment, spectrum, cv::DFT_COMPLEX_OUTPUT);
        for (int k = 0; k <= nfft / 2; ++k) {
            const cv::Vec2d value = spectrum.at<cv::Vec2d>(k, 0);
            const double magnitudeSquared = value[0] * value[0] + value[1] * value[1];
            double power = magnitudeSquared / (fsHz * windowEnergy);
            if (k > 0 && k < nfft / 2) {
                power *= 2.0;
            }
            accumulated[k] += power;
        }
        ++segmentCount;
        if (config.psdMode == CdimPsdMode::Periodogram) {
            break;
        }
    }

    if (segmentCount <= 0) {
        return output;
    }
    output.frequencyResolutionHz = fsHz / nfft;
    output.frequencyHz.resize(accumulated.size());
    output.psd.resize(accumulated.size());
    for (int k = 0; k < accumulated.size(); ++k) {
        output.frequencyHz[k] = k * output.frequencyResolutionHz;
        output.psd[k] = accumulated.at(k) / segmentCount;
    }
    return output;
}

double integratePsd(const QVector<double>& psd, double frequencyResolutionHz)
{
    if (psd.isEmpty() || !finite(frequencyResolutionHz) || frequencyResolutionHz <= 0.0) {
        return 0.0;
    }
    double integral = 0.0;
    for (double value : psd) {
        if (finite(value) && value >= 0.0) {
            integral += value;
        }
    }
    return integral * frequencyResolutionHz;
}

QVector<bool> detectPeaks(const QVector<double>& psd)
{
    QVector<bool> peaks(psd.size(), false);
    QVector<double> positiveValues;
    positiveValues.reserve(psd.size());
    for (double value : psd) {
        if (finite(value) && value > 0.0) {
            positiveValues.append(value);
        }
    }
    const double globalMedian = std::max(kEpsilon, median(positiveValues));
    for (int i = 1; i + 1 < psd.size(); ++i) {
        const double value = psd.at(i);
        if (!finite(value) || value <= 0.0) {
            continue;
        }
        QVector<double> local;
        const int psdSize = static_cast<int>(psd.size());
        for (int j = std::max(1, i - 4); j <= std::min(psdSize - 2, i + 4); ++j) {
            if (j != i && psd.at(j) > 0.0 && finite(psd.at(j))) {
                local.append(psd.at(j));
            }
        }
        const double localMedian = std::max(kEpsilon, median(local));
        if (value > 5.0 * localMedian && value > 10.0 * globalMedian) {
            peaks[i] = true;
        }
    }
    return peaks;
}

void setSelectedBand(CdimPsdChannelResult& channel,
                     const QVector<double>& frequencyHz,
                     const QVector<double>& psd,
                     const QVector<bool>& peaks,
                     double fsHz,
                     const CdimPsdAnalysisConfig& config,
                     int startBin,
                     int endBin)
{
    if (startBin < 0 || endBin < startBin || endBin >= psd.size()) {
        channel.qcStatus = QStringLiteral("No valid flat noise band");
        return;
    }
    const double minSpanHz = config.minimumNoiseBandNyquistWidth * fsHz * 0.5;
    const double spanHz = frequencyHz.at(endBin) - frequencyHz.at(startBin);
    const int binCount = endBin - startBin + 1;
    if (binCount < std::max(1, config.minimumNoiseBandBins) || spanHz < minSpanHz) {
        channel.qcStatus = QStringLiteral("Noise band is below the minimum bin/span requirement");
        return;
    }

    QVector<double> bandValues;
    QVector<double> logValues;
    for (int i = startBin; i <= endBin; ++i) {
        if (peaks.value(i, false)) {
            continue;
        }
        const double value = std::max(kEpsilon, psd.at(i));
        bandValues.append(value);
        logValues.append(std::log(value));
    }
    if (bandValues.size() < static_cast<qsizetype>(std::max(4, config.minimumNoiseBandBins / 2))) {
        channel.qcStatus = QStringLiteral("Noise band has too few non-peak bins");
        return;
    }

    const double n0 = median(bandValues);
    const double logMedian = median(logValues);
    double logDeviation = 0.0;
    for (double value : logValues) {
        logDeviation += std::abs(value - logMedian);
    }
    channel.noisePsdN0 = n0;
    channel.flatnessScore = logDeviation
                            / std::max(1.0, static_cast<double>(logValues.size()));
    channel.selectedNoiseStartBin = startBin;
    channel.selectedNoiseEndBin = endBin;
    channel.selectedNoiseStartHz = frequencyHz.at(startBin);
    channel.selectedNoiseEndHz = frequencyHz.at(endBin);
    channel.noiseBandValid = finite(n0) && n0 > 0.0;
    channel.qcStatus = channel.noiseBandValid
                           ? QStringLiteral("Noise band accepted")
                           : QStringLiteral("Noise floor is not positive");
}

void detectAutoBand(CdimPsdChannelResult& channel,
                    double fsHz,
                    const CdimPsdAnalysisConfig& config)
{
    if (channel.frequencyHz.isEmpty()) {
        return;
    }
    const QVector<bool> peaks = detectPeaks(channel.psd);
    for (int i = 0; i < peaks.size(); ++i) {
        if (peaks.at(i)) {
            channel.excludedPeakBins.append(i);
        }
    }

    const double nyquistHz = fsHz * 0.5;
    const double frequencyResolutionHz = channel.frequencyHz.size() > 1
                                             ? channel.frequencyHz.at(1)
                                             : 0.0;
    if (!finite(frequencyResolutionHz) || frequencyResolutionHz <= 0.0) {
        channel.qcStatus = QStringLiteral("PSD frequency resolution is invalid");
        return;
    }
    const int candidateStart = std::clamp(
        static_cast<int>(std::ceil(config.noiseCandidateStartNyquist * nyquistHz
                                   / frequencyResolutionHz)), 1,
        static_cast<int>(channel.frequencyHz.size()) - 1);
    const int candidateEnd = std::clamp(
        static_cast<int>(std::floor(config.noiseCandidateEndNyquist * nyquistHz
                                    / frequencyResolutionHz)), 1,
        static_cast<int>(channel.frequencyHz.size()) - 1);
    if (candidateEnd < candidateStart) {
        channel.qcStatus = QStringLiteral("Noise candidate range is empty");
        return;
    }

    int bestStart = -1;
    int bestEnd = -1;
    int runStart = -1;
    for (int i = candidateStart; i <= candidateEnd + 1; ++i) {
        const bool usable = i <= candidateEnd && !peaks.value(i, false);
        if (usable && runStart < 0) {
            runStart = i;
        }
        if ((!usable || i == candidateEnd + 1) && runStart >= 0) {
            const int runEnd = i - 1;
            if (runEnd - runStart + 1 > bestEnd - bestStart + 1) {
                bestStart = runStart;
                bestEnd = runEnd;
            }
            runStart = -1;
        }
    }
    setSelectedBand(channel, channel.frequencyHz, channel.psd, peaks,
                    fsHz, config, bestStart, bestEnd);
}

void fitFullSpectrum(CdimPsdChannelResult& channel,
                     double fsHz,
                     const CdimPsdAnalysisConfig& config)
{
    const QVector<bool> peaks = detectPeaks(channel.psd);
    for (int i = 0; i < peaks.size(); ++i) {
        if (peaks.at(i) && !channel.excludedPeakBins.contains(i)) {
            channel.excludedPeakBins.append(i);
        }
    }

    const double nyquistHz = fsHz * 0.5;
    const double minFitHz = std::max(channel.frequencyHz.value(1, 0.0), 0.02 * nyquistHz);
    const double maxFitHz = 0.95 * nyquistHz;
    QVector<double> frequencies;
    QVector<double> values;
    for (int i = 1; i < channel.frequencyHz.size(); ++i) {
        if (channel.frequencyHz.at(i) < minFitHz || channel.frequencyHz.at(i) > maxFitHz
            || peaks.value(i, false) || channel.psd.at(i) <= 0.0) {
            continue;
        }
        frequencies.append(channel.frequencyHz.at(i));
        values.append(channel.psd.at(i));
    }
    if (values.size() < 8) {
        channel.qcStatus = QStringLiteral("Full-spectrum fit has too few bins");
        return;
    }

    double bestError = std::numeric_limits<double>::infinity();
    double bestA = 0.0;
    double bestN0 = 0.0;
    double bestBeta = 0.0;
    for (int betaStep = 1; betaStep <= 100; ++betaStep) {
        const double beta = 0.05 * betaStep;
        double sumXX = 0.0;
        double sumX = 0.0;
        double sumXY = 0.0;
        double sumY = 0.0;
        for (int i = 0; i < frequencies.size(); ++i) {
            const double x = std::pow(frequencies.at(i), -beta);
            sumXX += x * x;
            sumX += x;
            sumXY += x * values.at(i);
            sumY += values.at(i);
        }
        const double n = frequencies.size();
        const double determinant = sumXX * n - sumX * sumX;
        if (std::abs(determinant) < kEpsilon) {
            continue;
        }
        const double a = (sumXY * n - sumX * sumY) / determinant;
        const double n0 = (sumXX * sumY - sumX * sumXY) / determinant;
        if (!finite(a) || !finite(n0) || a < 0.0 || n0 <= 0.0) {
            continue;
        }
        double error = 0.0;
        for (int i = 0; i < frequencies.size(); ++i) {
            const double prediction = a * std::pow(frequencies.at(i), -beta) + n0;
            const double residual = values.at(i) - prediction;
            error += residual * residual;
        }
        if (error < bestError) {
            bestError = error;
            bestA = a;
            bestN0 = n0;
            bestBeta = beta;
        }
    }
    if (!finite(bestError) || bestN0 <= 0.0) {
        channel.qcStatus = QStringLiteral("Full-spectrum fit failed");
        return;
    }

    channel.fitValid = true;
    channel.fitBeta = bestBeta;
    channel.noisePsdN0 = bestN0;
    channel.fittedTotalPsd.resize(channel.frequencyHz.size());
    channel.fittedAtmosphericPsd.resize(channel.frequencyHz.size());
    double totalVariance = 0.0;
    double residualVariance = 0.0;
    double meanValue = 0.0;
    int fitCount = 0;
    for (int i = 0; i < channel.frequencyHz.size(); ++i) {
        const double frequency = channel.frequencyHz.at(i);
        const double atmosphere = frequency > 0.0 ? bestA * std::pow(frequency, -bestBeta) : 0.0;
        channel.fittedAtmosphericPsd[i] = atmosphere;
        channel.fittedTotalPsd[i] = atmosphere + bestN0;
        if (frequency >= minFitHz && frequency <= maxFitHz && !peaks.value(i, false)) {
            const double residual = channel.psd.at(i) - channel.fittedTotalPsd.at(i);
            residualVariance += residual * residual;
            meanValue += channel.psd.at(i);
            ++fitCount;
        }
    }
    meanValue /= std::max(1, fitCount);
    for (int i = 1; i < channel.frequencyHz.size(); ++i) {
        if (channel.frequencyHz.at(i) >= minFitHz && channel.frequencyHz.at(i) <= maxFitHz
            && !peaks.value(i, false)) {
            const double difference = channel.psd.at(i) - meanValue;
            totalVariance += difference * difference;
        }
    }
    channel.fitR2 = totalVariance > kEpsilon ? 1.0 - residualVariance / totalVariance : 0.0;

    const double candidateStartHz = config.noiseCandidateStartNyquist * nyquistHz;
    const double candidateEndHz = config.noiseCandidateEndNyquist * nyquistHz;
    int bandStart = -1;
    int bandEnd = -1;
    for (int i = 1; i < channel.frequencyHz.size(); ++i) {
        const double frequency = channel.frequencyHz.at(i);
        const bool noiseDominated = frequency >= candidateStartHz && frequency <= candidateEndHz
                                     && channel.fittedAtmosphericPsd.at(i)
                                            <= config.fitNoiseDominanceKappa * bestN0;
        if (noiseDominated && !peaks.value(i, false)) {
            if (bandStart < 0) {
                bandStart = i;
            }
            bandEnd = i;
        } else if (bandStart >= 0) {
            break;
        }
    }
    setSelectedBand(channel, channel.frequencyHz, channel.psd, peaks,
                    fsHz, config, bandStart, bandEnd);
    if (!channel.noiseBandValid) {
        // A fitted atmosphere curve can remain above the strict dominance
        // threshold throughout the candidate band. Keep the model-derived N0
        // but use the same validated high-frequency fallback band as auto mode
        // so the correction can still be audited and visualized.
        const double fittedN0 = bestN0;
        detectAutoBand(channel, fsHz, config);
        if (channel.noiseBandValid) {
            channel.noisePsdN0 = fittedN0;
            channel.qcStatus = QStringLiteral("Full-spectrum fit accepted with fallback noise band");
        }
    }
    if (channel.noiseBandValid) {
        channel.qcStatus = QStringLiteral("Full-spectrum fit and noise band accepted");
    }
}

CdimPsdChannelResult analyzeChannel(const QVector<double>& values,
                                    double fsHz,
                                    const CdimPsdAnalysisConfig& config,
                                    CdimPsdAnalysisResult& result)
{
    CdimPsdChannelResult channel;
    if (values.size() < 8) {
        channel.qcStatus = QStringLiteral("Insufficient samples for PSD analysis");
        return channel;
    }

    double meanValue = 0.0;
    for (double value : values) {
        meanValue += value;
    }
    meanValue /= values.size();
    QVector<double> centered;
    centered.reserve(values.size());
    double variance = 0.0;
    for (double value : values) {
        const double centeredValue = value - meanValue;
        centered.append(centeredValue);
        variance += centeredValue * centeredValue;
    }
    variance /= values.size();
    channel.measuredVariancePx2 = variance;

    const PsdData psd = calculatePsd(centered, fsHz, config, result);
    channel.frequencyHz = psd.frequencyHz;
    channel.psd = psd.psd;
    channel.psdIntegralVariancePx2 = integratePsd(channel.psd, psd.frequencyResolutionHz);
    if (channel.frequencyHz.size() < 3) {
        channel.qcStatus = QStringLiteral("PSD has too few frequency bins");
        return channel;
    }

    if (config.noiseDetectionMode == CdimNoiseDetectionMode::FullSpectrumFit) {
        fitFullSpectrum(channel, fsHz, config);
    } else {
        detectAutoBand(channel, fsHz, config);
    }

    if (channel.noiseBandValid) {
        channel.noiseVariancePx2 = channel.noisePsdN0 * fsHz * 0.5;
        channel.atmosphericVariancePx2 = channel.measuredVariancePx2
                                         - channel.noiseVariancePx2;
        channel.correctionValid = finite(channel.atmosphericVariancePx2)
                                  && channel.atmosphericVariancePx2 > 0.0
                                  && channel.noiseVariancePx2 < channel.measuredVariancePx2;
        if (!channel.correctionValid) {
            channel.qcStatus = QStringLiteral("Estimated noise is not below measured variance");
        }
    }
    return channel;
}

} // namespace

CdimPsdAnalysisResult CdimPsdAnalysis::analyze(const CdimPsdInput& input,
                                               const CdimPsdAnalysisConfig& config)
{
    CdimPsdAnalysisResult result;
    result.enabled = config.enabled;
    result.psdMode = config.psdMode;
    result.noiseDetectionMode = config.noiseDetectionMode;

    if (!config.enabled) {
        warning(result, QStringLiteral("PSD analysis disabled; measured centroid variance path retained"));
        return result;
    }

    const int sampleCount = static_cast<int>(std::min(input.longitudinal.size(),
                                                      input.transverse.size()));
    QVector<double> longitudinal;
    QVector<double> transverse;
    QVector<quint64> timestamps;
    QVector<quint64> frameIds;
    longitudinal.reserve(sampleCount);
    transverse.reserve(sampleCount);
    timestamps.reserve(sampleCount);
    frameIds.reserve(sampleCount);
    for (int i = 0; i < sampleCount; ++i) {
        const double longitudinalValue = input.longitudinal.at(i);
        const double transverseValue = input.transverse.at(i);
        if (!finite(longitudinalValue) || !finite(transverseValue)) {
            continue;
        }
        longitudinal.append(longitudinalValue);
        transverse.append(transverseValue);
        timestamps.append(input.cameraTimestamps.value(i, 0));
        frameIds.append(input.frameIds.value(i, 0));
    }
    result.sampleCount = longitudinal.size();
    if (result.sampleCount < 2) {
        warning(result, QStringLiteral("Insufficient paired centroid samples"));
        return result;
    }

    const SamplingInfo sampling = inferSampling(input, config, result.sampleCount,
                                                timestamps, frameIds, result);
    result.fsActualHz = sampling.fsHz;
    result.fsSource = sampling.source;
    result.relativeTimingJitter = sampling.relativeJitter;
    result.samplingQcPassed = sampling.fsHz > 0.0 && sampling.timingJitterAcceptable;
    if (!result.samplingQcPassed) {
        return result;
    }

    result.nyquistHz = sampling.fsHz * 0.5;
    result.longitudinal = analyzeChannel(longitudinal, sampling.fsHz, config, result);
    result.transverse = analyzeChannel(transverse, sampling.fsHz, config, result);
    result.frequencyResolutionHz = result.longitudinal.frequencyHz.size() > 1
                                       ? result.longitudinal.frequencyHz.at(1)
                                       : 0.0;
    result.valid = result.longitudinal.correctionValid && result.transverse.correctionValid;
    if (!result.valid) {
        warning(result, QStringLiteral("PSD correction is invalid; measured variance must be used for r0"));
    }
    return result;
}
