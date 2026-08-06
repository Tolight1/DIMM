#include "AlignmentCoarseEstimator.h"

#include <algorithm>
#include <cmath>

#include <QtGlobal>

using PolarisDetectionPipeline::InitialStarCandidate;

namespace {

double pointDistance(const QPointF& a, const QPointF& b)
{
    const double dx = a.x() - b.x();
    const double dy = a.y() - b.y();
    return std::sqrt(dx * dx + dy * dy);
}

double medianOf(QVector<double> values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const int mid = values.size() / 2;
    return values.size() % 2 == 0 ? 0.5 * (values[mid - 1] + values[mid]) : values[mid];
}

QVector<InitialStarCandidate> limitedCandidates(QVector<InitialStarCandidate> candidates,
                                                int maxCandidates)
{
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) {
                  return a.signal == b.signal ? a.peak > b.peak : a.signal > b.signal;
              });
    if (candidates.size() > maxCandidates) {
        candidates.resize(maxCandidates);
    }
    for (int i = 0; i < candidates.size(); ++i) {
        candidates[i].index = i + 1;
    }
    return candidates;
}

bool fitTrackVelocity(AlignmentCoarseTracker::Track* track)
{
    if (!track) {
        return false;
    }

    track->velocityFitValid = false;
    track->velocityPxSec = QPointF();
    track->speedPxSec = 0.0;
    track->durationSec = 0.0;
    track->displacementPx = 0.0;
    track->fitRmsPx = 0.0;

    if (track->points.size() < 2) {
        return false;
    }

    track->durationSec =
        static_cast<double>(track->points.last().timestampMs -
                            track->points.first().timestampMs) /
        1000.0;
    track->displacementPx =
        pointDistance(track->points.first().positionPx,
                      track->points.last().positionPx);

    const qint64 t0Ms = track->points.first().timestampMs;
    double meanT = 0.0;
    double meanX = 0.0;
    double meanY = 0.0;
    for (const auto& point : track->points) {
        const double t =
            static_cast<double>(point.timestampMs - t0Ms) / 1000.0;
        meanT += t;
        meanX += point.positionPx.x();
        meanY += point.positionPx.y();
    }

    const double n = static_cast<double>(track->points.size());
    meanT /= n;
    meanX /= n;
    meanY /= n;

    double denominator = 0.0;
    double vxNumerator = 0.0;
    double vyNumerator = 0.0;
    for (const auto& point : track->points) {
        const double t =
            static_cast<double>(point.timestampMs - t0Ms) / 1000.0;
        const double dt = t - meanT;
        denominator += dt * dt;
        vxNumerator += dt * (point.positionPx.x() - meanX);
        vyNumerator += dt * (point.positionPx.y() - meanY);
    }

    if (denominator <= 1e-9) {
        return false;
    }

    const double vx = vxNumerator / denominator;
    const double vy = vyNumerator / denominator;
    track->velocityPxSec = QPointF(vx, vy);
    track->speedPxSec = std::sqrt(vx * vx + vy * vy);

    double residualSum = 0.0;
    for (const auto& point : track->points) {
        const double t =
            static_cast<double>(point.timestampMs - t0Ms) / 1000.0;
        const double predictedX = meanX + vx * (t - meanT);
        const double predictedY = meanY + vy * (t - meanT);
        const double dx = point.positionPx.x() - predictedX;
        const double dy = point.positionPx.y() - predictedY;
        residualSum += dx * dx + dy * dy;
    }

    track->fitRmsPx = std::sqrt(residualSum / n);
    track->velocityFitValid = true;
    return true;
}

QString trackRejectionReason(
    const AlignmentCoarseTracker::Track& track,
    const CoarseAlignmentConfig& config)
{
    if (track.points.size() < config.minTrackPoints) {
        return QStringLiteral("采样点%1个，少于要求%2个")
            .arg(track.points.size())
            .arg(config.minTrackPoints);
    }

    if (!track.velocityFitValid) {
        return QStringLiteral("速度拟合失败，时间戳可能重复或采样时间跨度不足");
    }

    if (track.durationSec < config.minTrackDurationSec) {
        return QStringLiteral("持续时间%1s，少于要求%2s")
            .arg(track.durationSec, 0, 'f', 1)
            .arg(config.minTrackDurationSec, 0, 'f', 1);
    }

    if (track.displacementPx < config.minTrackDisplacementPx) {
        return QStringLiteral("净位移%1px，小于要求%2px")
            .arg(track.displacementPx, 0, 'f', 2)
            .arg(config.minTrackDisplacementPx, 0, 'f', 2);
    }

    if (track.fitRmsPx > config.maxTrackFitRmsPx) {
        return QStringLiteral("线性拟合RMS %1px，大于允许值%2px")
            .arg(track.fitRmsPx, 0, 'f', 2)
            .arg(config.maxTrackFitRmsPx, 0, 'f', 2);
    }

    if (track.speedPxSec <= config.minTrackSpeedPxSec) {
        return QStringLiteral("拟合速度%1px/s，不大于要求%2px/s")
            .arg(track.speedPxSec, 0, 'f', 4)
            .arg(config.minTrackSpeedPxSec, 0, 'f', 4);
    }

    return QString();
}

const AlignmentCoarseTracker::Track* selectDiagnosticTrack(
    const QVector<AlignmentCoarseTracker::Track>& tracks)
{
    const AlignmentCoarseTracker::Track* best = nullptr;

    for (const auto& track : tracks) {
        if (track.points.isEmpty()) {
            continue;
        }

        if (!best) {
            best = &track;
            continue;
        }

        if (track.durationSec > best->durationSec) {
            best = &track;
            continue;
        }

        if (qFuzzyCompare(track.durationSec + 1.0,
                          best->durationSec + 1.0) &&
            track.points.size() > best->points.size()) {
            best = &track;
        }
    }

    return best;
}

QString buildTrackDiagnosticText(
    const AlignmentCoarseTracker::Track* track)
{
    if (!track) {
        return QString();
    }

    QString text =
        QStringLiteral("最长轨迹: %1点 / %2s / 净位移%3px / "
                       "拟合速度%4px/s / RMS %5px")
            .arg(track->points.size())
            .arg(track->durationSec, 0, 'f', 1)
            .arg(track->displacementPx, 0, 'f', 2)
            .arg(track->speedPxSec, 0, 'f', 4)
            .arg(track->fitRmsPx, 0, 'f', 2);

    if (!track->rejectionReason.isEmpty()) {
        text += QStringLiteral(" | 未通过: %1")
                    .arg(track->rejectionReason);
    } else if (track->usedForSolve) {
        text += QStringLiteral(" | 已通过轨迹筛选");
    }

    return text;
}

bool solveNorthCelestialPoleCenter(const QVector<AlignmentCoarseTracker::Track*>& tracks,
                                   const CoarseAlignmentConfig& config,
                                   QPointF* center,
                                   double* residualRmsPx)
{
    double a00 = 0.0;
    double a01 = 0.0;
    double a11 = 0.0;
    double b0 = 0.0;
    double b1 = 0.0;

    for (const auto* track : tracks) {
        const QPointF p = track->points[track->points.size() / 2].positionPx;
        const double vx = track->velocityPxSec.x();
        const double vy = track->velocityPxSec.y();
        const double rhs = vx * p.x() + vy * p.y();
        const double weight = std::max(0.1, track->durationSec) /
                              (1.0 + track->fitRmsPx * track->fitRmsPx);
        a00 += weight * vx * vx;
        a01 += weight * vx * vy;
        a11 += weight * vy * vy;
        b0 += weight * vx * rhs;
        b1 += weight * vy * rhs;
    }

    const double det = a00 * a11 - a01 * a01;
    if (std::abs(det) < 1e-9) {
        return false;
    }

    const double cx = (b0 * a11 - b1 * a01) / det;
    const double cy = (a00 * b1 - a01 * b0) / det;
    *center = QPointF(cx, cy);

    double residualSum = 0.0;
    for (const auto* track : tracks) {
        const QPointF p = track->points[track->points.size() / 2].positionPx;
        const double speed = std::max(1e-9, track->speedPxSec);
        const double nx = track->velocityPxSec.x() / speed;
        const double ny = track->velocityPxSec.y() / speed;
        const double residual = (p.x() - cx) * nx + (p.y() - cy) * ny;
        residualSum += residual * residual;
    }
    *residualRmsPx = std::sqrt(residualSum / std::max<qsizetype>(1, tracks.size()));
    return true;
}

} // namespace

void AlignmentCoarseTracker::reset()
{
    m_tracks.clear();
    m_nextTrackId = 1;
}

CoarseAlignmentEstimate AlignmentCoarseTracker::addFrame(int cameraIndex,
                                                         quint64 generation,
                                                         quint64 frameId,
                                                         qint64 timestampMs,
                                                         const QSize& frameSize,
                                                         QVector<InitialStarCandidate> candidates,
                                                         const CoarseAlignmentConfig& config)
{
    CoarseAlignmentEstimate estimate;
    estimate.cameraIndex = cameraIndex;
    estimate.generation = generation;
    estimate.frameId = frameId;
    estimate.frameSize = frameSize;
    estimate.requiredTrackCount = config.minTracksForCenter;
    estimate.detectedCandidateCount = candidates.size();
    if (estimate.detectedCandidateCount > config.maxCandidates) {
        estimate.tooManyCandidates = true;
    }

    candidates = limitedCandidates(std::move(candidates), config.maxCandidates);
    estimate.acceptedCandidateCount = candidates.size();

    QVector<bool> trackAssigned(m_tracks.size(), false);
    QVector<bool> candidateAssigned(candidates.size(), false);

    for (int ci = 0; ci < candidates.size(); ++ci) {
        double bestDistance = config.maxAssociationDistancePx;
        int bestTrack = -1;
        for (int ti = 0; ti < m_tracks.size(); ++ti) {
            if (trackAssigned[ti]) {
                continue;
            }
            Track& track = m_tracks[ti];
            if (track.points.isEmpty()) {
                continue;
            }
            const double distance = pointDistance(track.points.last().positionPx,
                                                  candidates[ci].center);
            if (distance <= bestDistance) {
                bestDistance = distance;
                bestTrack = ti;
            }
        }
        if (bestTrack >= 0) {
            Track& track = m_tracks[bestTrack];
            TrackPoint point;
            point.timestampMs = timestampMs;
            point.positionPx = candidates[ci].center;
            point.signal = candidates[ci].signal;
            track.points.append(point);
            track.lastTimestampMs = timestampMs;
            trackAssigned[bestTrack] = true;
            candidateAssigned[ci] = true;
        }
    }

    for (int ci = 0; ci < candidates.size(); ++ci) {
        if (candidateAssigned[ci]) {
            continue;
        }
        Track track;
        track.id = m_nextTrackId++;
        TrackPoint point;
        point.timestampMs = timestampMs;
        point.positionPx = candidates[ci].center;
        point.signal = candidates[ci].signal;
        track.points.append(point);
        track.lastTimestampMs = timestampMs;
        m_tracks.append(track);
    }

    for (Track& track : m_tracks) {
        while (track.points.size() > config.maxTrackPoints) {
            track.points.removeFirst();
        }
    }

    m_tracks.erase(
        std::remove_if(m_tracks.begin(), m_tracks.end(),
                       [&](const Track& track) {
                           return timestampMs - track.lastTimestampMs >
                                  static_cast<qint64>(config.maxStaleTrackSec * 1000.0);
                       }),
        m_tracks.end());

    estimate.activeTrackCount = m_tracks.size();

    for (Track& track : m_tracks) {
        track.usedForSolve = false;
        track.rejectionReason.clear();
        fitTrackVelocity(&track);
    }

    QVector<Track*> usableTracks;
    QVector<double> fittedSpeeds;

    for (Track& track : m_tracks) {
        if (track.velocityFitValid) {
            fittedSpeeds.append(track.speedPxSec);
        }

        track.rejectionReason = trackRejectionReason(track, config);
        if (!track.rejectionReason.isEmpty()) {
            continue;
        }

        track.usedForSolve = true;
        usableTracks.append(&track);
    }

    estimate.fittedTrackCount = fittedSpeeds.size();
    estimate.medianFittedSpeedPxSec = medianOf(fittedSpeeds);
    estimate.usableTrackCount = usableTracks.size();

    QVector<double> usableSpeeds;
    for (const Track* track : usableTracks) {
        usableSpeeds.append(track->speedPxSec);
    }
    estimate.medianSpeedPxSec = medianOf(usableSpeeds);

    bool solvedCenter = false;
    QPointF solvedCenterPx;
    if (usableTracks.size() >= config.minTracksForCenter) {
        solvedCenter = solveNorthCelestialPoleCenter(usableTracks, config, &solvedCenterPx,
                                                     &estimate.centerResidualRmsPx);
    }

    estimate.frameCenterPx =
        QPointF(frameSize.width() * 0.5, frameSize.height() * 0.5);

    if (!usableTracks.isEmpty()) {
        const double medianSpeedArcsecSec =
            estimate.medianSpeedPxSec * config.plateScaleArcsecPx;
        const double ratio =
            std::clamp(medianSpeedArcsecSec / config.siderealArcsecSec,
                       0.0,
                       1.0);
        estimate.medianPolarDistanceDegFromSpeed =
            qRadiansToDegrees(std::asin(ratio));
    }

    if (solvedCenter) {
        estimate.northCelestialPolePx = solvedCenterPx;
        estimate.adjustmentVectorPx =
            estimate.northCelestialPolePx - estimate.frameCenterPx;
        estimate.offsetPx =
            pointDistance(estimate.northCelestialPolePx,
                          estimate.frameCenterPx);
        estimate.offsetDeg =
            estimate.offsetPx * config.plateScaleArcsecPx / 3600.0;
    }

    for (const Track& track : m_tracks) {
        if (track.points.isEmpty()) {
            continue;
        }
        CoarseAlignmentTrackOverlay overlayTrack;
        overlayTrack.id = track.id;
        overlayTrack.pointCount = track.points.size();
        overlayTrack.startPx = track.points.first().positionPx;
        overlayTrack.endPx = track.points.last().positionPx;
        overlayTrack.velocityPxSec = track.velocityPxSec;
        overlayTrack.speedPxSec = track.speedPxSec;
        overlayTrack.durationSec = track.durationSec;
        overlayTrack.displacementPx = track.displacementPx;
        overlayTrack.fitRmsPx = track.fitRmsPx;
        overlayTrack.velocityFitValid = track.velocityFitValid;
        overlayTrack.usedForSolve = track.usedForSolve;
        overlayTrack.rejectionReason = track.rejectionReason;
        estimate.tracks.append(overlayTrack);
    }

    estimate.diagnosticText =
        buildTrackDiagnosticText(selectDiagnosticTrack(m_tracks));

    if (estimate.detectedCandidateCount > config.maxCandidates) {
        estimate.tooManyCandidates = true;
    }
    if (estimate.detectedCandidateCount <= 0) {
        estimate.tooFewTracks = true;
        estimate.statusText =
            QStringLiteral("粗对准: 未检测到星点，请检查全画幅找星阈值、"
                           "光斑面积和Mono12原始帧");
    } else if (estimate.activeTrackCount <= 0) {
        estimate.tooFewTracks = true;
        estimate.statusText =
            QStringLiteral("粗对准: 已检测到候选星点，但尚未建立连续轨迹");
    } else if (usableTracks.size() < config.minTracksForCenter) {
        estimate.tooFewTracks = true;

        if (usableTracks.size() == 1) {
            estimate.statusText =
                QStringLiteral("粗对准: 已获得1/%1条可用轨迹，"
                               "速度%2px/s，还需至少1条方向不同的轨迹")
                    .arg(config.minTracksForCenter)
                    .arg(estimate.medianSpeedPxSec, 0, 'f', 4);
        } else if (estimate.fittedTrackCount > 0) {
            estimate.statusText =
                QStringLiteral("粗对准: 活动轨迹%1条，已拟合%2条，"
                               "可用0/%3；请查看未通过原因")
                    .arg(estimate.activeTrackCount)
                    .arg(estimate.fittedTrackCount)
                    .arg(config.minTracksForCenter);
        } else {
            estimate.statusText =
                QStringLiteral("粗对准: 活动轨迹%1条，正在积累采样点和时间")
                    .arg(estimate.activeTrackCount);
        }
    } else if (!solvedCenter) {
        estimate.centerIllConditioned = true;
        estimate.statusText =
            QStringLiteral("粗对准: 已有%1条可用轨迹，但轨迹方向接近平行；"
                           "请延长采样或使用绕同一中心旋转的星场")
                .arg(usableTracks.size());
    } else if (estimate.centerResidualRmsPx >
               config.maxCenterResidualRmsPx) {
        estimate.statusText =
            QStringLiteral("粗对准: 圆心估计不稳定 RMS %1px")
                .arg(estimate.centerResidualRmsPx, 0, 'f', 1);
    } else {
        estimate.valid = true;
        estimate.statusText =
            QStringLiteral("粗对准: 北天极距中心%1px / %2°，"
                           "可用轨迹%3条")
                .arg(estimate.offsetPx, 0, 'f', 0)
                .arg(estimate.offsetDeg, 0, 'f', 2)
                .arg(usableTracks.size());
    }

    return estimate;
}
