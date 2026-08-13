#pragma once

#include "PolarisDetectionPipeline.h"

#include <QPointF>
#include <QRectF>
#include <QMetaType>
#include <QSize>
#include <QString>
#include <QVector>

struct CoarseAlignmentConfig {
    int maxCandidates = 80;
    double maxAssociationDistancePx = 25.0;
    double maxStaleTrackSec = 5.0;
    int maxTrackPoints = 90;
    int minTrackPoints = 5;
    double minTrackDurationSec = 15.0;
    double minTrackDisplacementPx = 2.0;
    double maxTrackFitRmsPx = 3.5;
    double minTrackSpeedPxSec = 0.005;
    double maxCenterResidualRmsPx = 80.0;
    double plateScaleArcsecPx = 1.917;
    double siderealArcsecSec = 15.041;
    int minTracksForCenter = 2;
};

struct CoarseAlignmentTrackOverlay {
    int id = 0;
    int pointCount = 0;
    QPointF startPx;
    QPointF endPx;
    QPointF velocityPxSec;
    double speedPxSec = 0.0;
    double durationSec = 0.0;
    double displacementPx = 0.0;
    double fitRmsPx = 0.0;
    bool velocityFitValid = false;
    bool usedForSolve = false;
    QString rejectionReason;
};

struct CoarseAlignmentEstimate {
    int cameraIndex = 0;
    quint64 generation = 0;
    quint64 frameId = 0;
    bool valid = false;
    bool centerIllConditioned = false;
    bool tooFewTracks = false;
    bool tooManyCandidates = false;
    QSize frameSize;
    QPointF northCelestialPolePx;
    QPointF frameCenterPx;
    QPointF adjustmentVectorPx;
    double offsetPx = 0.0;
    double offsetDeg = 0.0;
    double medianSpeedPxSec = 0.0;
    double medianFittedSpeedPxSec = 0.0;
    double medianPolarDistanceDegFromSpeed = 0.0;
    double centerResidualRmsPx = 0.0;
    int detectedCandidateCount = 0;
    int acceptedCandidateCount = 0;
    int activeTrackCount = 0;
    int fittedTrackCount = 0;
    int usableTrackCount = 0;
    int requiredTrackCount = 0;
    double processingMs = 0.0;
    QString statusText;
    QString diagnosticText;
    QVector<CoarseAlignmentTrackOverlay> tracks;
};

class AlignmentCoarseTracker final {
public:
    struct TrackPoint {
        qint64 timestampMs = 0;
        QPointF positionPx;
        double signal = 0.0;
    };

    struct Track {
        int id = 0;
        QVector<TrackPoint> points;
        QPointF velocityPxSec;
        double speedPxSec = 0.0;
        double durationSec = 0.0;
        double displacementPx = 0.0;
        double fitRmsPx = 0.0;
        qint64 lastTimestampMs = 0;
        bool velocityFitValid = false;
        bool usedForSolve = false;
        QString rejectionReason;
    };

    void reset();
    CoarseAlignmentEstimate addFrame(int cameraIndex,
                                     quint64 generation,
                                     quint64 frameId,
                                     qint64 timestampMs,
                                     const QSize& frameSize,
                                     QVector<PolarisDetectionPipeline::InitialStarCandidate> candidates,
                                     const CoarseAlignmentConfig& config);

private:
    int m_nextTrackId = 1;
    QVector<Track> m_tracks;
};

Q_DECLARE_METATYPE(CoarseAlignmentEstimate)
