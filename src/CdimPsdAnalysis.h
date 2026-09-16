#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

#include <QtGlobal>

enum class CdimPsdMode {
    Periodogram = 0,
    Welch = 1,
};

enum class CdimNoiseDetectionMode {
    AutoFlatRegion = 0,
    FullSpectrumFit = 1,
};

enum class CdimPsdFsSource {
    Timestamp = 0,
    ConfiguredFallback = 1,
};

struct CdimPsdAnalysisConfig {
    bool enabled = true;
    CdimPsdMode psdMode = CdimPsdMode::Welch;
    CdimNoiseDetectionMode noiseDetectionMode = CdimNoiseDetectionMode::AutoFlatRegion;
    int welchSegmentLength = 400;
    double welchOverlap = 0.50;
    int nfft = 400;
    double noiseCandidateStartNyquist = 0.60;
    double noiseCandidateEndNyquist = 0.90;
    int minimumNoiseBandBins = 16;
    double minimumNoiseBandNyquistWidth = 0.05;
    double fitNoiseDominanceKappa = 1.0;
    double timestampTickUs = 0.008;
    double maxRelativeTimingJitter = 0.05;
};

struct CdimPsdInput {
    QVector<double> longitudinal;
    QVector<double> transverse;
    QVector<quint64> cameraTimestamps;
    QVector<quint64> frameIds;
    double configuredFrameRateHz = 0.0;
};

struct CdimPsdChannelResult {
    QVector<double> frequencyHz;
    QVector<double> psd;
    QVector<double> fittedTotalPsd;
    QVector<double> fittedAtmosphericPsd;
    QVector<int> excludedPeakBins;

    double measuredVariancePx2 = 0.0;
    double psdIntegralVariancePx2 = 0.0;
    double noisePsdN0 = 0.0;
    double noiseVariancePx2 = 0.0;
    double atmosphericVariancePx2 = 0.0;
    double selectedNoiseStartHz = 0.0;
    double selectedNoiseEndHz = 0.0;
    double flatnessScore = 0.0;
    double fitBeta = 0.0;
    double fitR2 = 0.0;

    int selectedNoiseStartBin = -1;
    int selectedNoiseEndBin = -1;
    bool noiseBandValid = false;
    bool fitValid = false;
    bool correctionValid = false;
    QString qcStatus;
};

struct CdimPsdAnalysisResult {
    bool enabled = true;
    CdimPsdMode psdMode = CdimPsdMode::Welch;
    CdimNoiseDetectionMode noiseDetectionMode = CdimNoiseDetectionMode::AutoFlatRegion;
    CdimPsdFsSource fsSource = CdimPsdFsSource::ConfiguredFallback;

    double fsActualHz = 0.0;
    double nyquistHz = 0.0;
    double frequencyResolutionHz = 0.0;
    double relativeTimingJitter = 0.0;
    int sampleCount = 0;
    bool samplingQcPassed = false;
    bool valid = false;
    QStringList warnings;

    CdimPsdChannelResult longitudinal;
    CdimPsdChannelResult transverse;
};

class CdimPsdAnalysis final
{
public:
    static CdimPsdAnalysisResult analyze(const CdimPsdInput& input,
                                         const CdimPsdAnalysisConfig& config = {});
};

Q_DECLARE_METATYPE(CdimPsdAnalysisConfig)
Q_DECLARE_METATYPE(CdimPsdAnalysisResult)
