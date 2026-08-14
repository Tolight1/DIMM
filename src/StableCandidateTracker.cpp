#include "StableCandidateTracker.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr qint64 kMaxMissingFramesToKeep = 10;

double distancePx(const QPointF& a, const QPointF& b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

struct CandidateTrackMatch {
    int candidateIndex = -1;
    int trackIndex = -1;
    double distance = 0.0;
};

} // namespace

QVector<PolarisDetectionPipeline::InitialStarCandidate> StableCandidateTracker::update(
    const QVector<PolarisDetectionPipeline::InitialStarCandidate>& candidates)
{
    QVector<PolarisDetectionPipeline::InitialStarCandidate> stable = candidates;

    QVector<CandidateTrackMatch> possibleMatches;
    for (int candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
        const auto& candidate = candidates[candidateIndex];
        const double radius = matchRadiusPx(candidate);
        for (int trackIndex = 0; trackIndex < m_tracks.size(); ++trackIndex) {
            const double distance = distancePx(candidate.center, m_tracks[trackIndex].center);
            if (distance <= radius) {
                possibleMatches.push_back(CandidateTrackMatch{candidateIndex, trackIndex, distance});
            }
        }
    }

    std::sort(possibleMatches.begin(),
              possibleMatches.end(),
              [](const CandidateTrackMatch& lhs, const CandidateTrackMatch& rhs) {
                  return lhs.distance < rhs.distance;
              });

    QVector<int> matchedTrackByCandidate(candidates.size(), -1);
    QVector<bool> trackMatched(m_tracks.size(), false);
    for (const CandidateTrackMatch& match : possibleMatches) {
        if (matchedTrackByCandidate[match.candidateIndex] >= 0 ||
            trackMatched[match.trackIndex]) {
            continue;
        }
        matchedTrackByCandidate[match.candidateIndex] = match.trackIndex;
        trackMatched[match.trackIndex] = true;
    }

    QVector<Track> nextTracks;
    nextTracks.reserve(m_tracks.size() + candidates.size());

    for (int candidateIndex = 0; candidateIndex < stable.size(); ++candidateIndex) {
        const int matchedTrackIndex = matchedTrackByCandidate[candidateIndex];
        Track track;
        if (matchedTrackIndex >= 0) {
            track = m_tracks[matchedTrackIndex];
        } else {
            track.id = m_nextId++;
        }
        track.center = stable[candidateIndex].center;
        track.bbox = stable[candidateIndex].bbox;
        track.missingFrames = 0;
        stable[candidateIndex].index = track.id;
        nextTracks.push_back(track);
    }

    for (int trackIndex = 0; trackIndex < m_tracks.size(); ++trackIndex) {
        if (trackMatched[trackIndex]) {
            continue;
        }
        Track track = m_tracks[trackIndex];
        ++track.missingFrames;
        if (track.missingFrames <= kMaxMissingFramesToKeep) {
            nextTracks.push_back(track);
        }
    }

    m_tracks = nextTracks;
    return stable;
}

void StableCandidateTracker::clear()
{
    m_tracks.clear();
    m_nextId = 1;
}

double StableCandidateTracker::matchRadiusPx(
    const PolarisDetectionPipeline::InitialStarCandidate& candidate)
{
    const double bboxDiagonal =
        std::hypot(static_cast<double>(candidate.bbox.width()),
                   static_cast<double>(candidate.bbox.height()));
    return std::max(64.0, bboxDiagonal);
}
