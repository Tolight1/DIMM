#pragma once

#include "PolarisDetectionPipeline.h"

#include <QPointF>
#include <QRect>
#include <QVector>

class StableCandidateTracker {
public:
    QVector<PolarisDetectionPipeline::InitialStarCandidate> update(
        const QVector<PolarisDetectionPipeline::InitialStarCandidate>& candidates);
    void clear();
    int nextId() const { return m_nextId; }

private:
    struct Track {
        int id = 0;
        QPointF center;
        QRect bbox;
        qint64 missingFrames = 0;
    };

    QVector<Track> m_tracks;
    int m_nextId = 1;

    static double matchRadiusPx(const PolarisDetectionPipeline::InitialStarCandidate& candidate);
};
