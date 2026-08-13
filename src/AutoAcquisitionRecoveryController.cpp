#include "AutoAcquisitionRecoveryController.h"

#include <algorithm>

namespace {

qint64 intervalMsFromMinutes(int minutes)
{
    return static_cast<qint64>(std::clamp(minutes, 1, 120)) * 60 * 1000;
}

} // namespace

void AutoAcquisitionRecoveryController::reset()
{
    m_phase = AutoAcquisitionRecoveryPhase::Idle;
    m_windowId.clear();
    m_lastScanStartedMs = -1;
    m_lastScanFinishedMs = -1;
}

void AutoAcquisitionRecoveryController::enterWindow(const QString& windowId, qint64 nowMs)
{
    if (windowId.isEmpty()) {
        reset();
        return;
    }
    if (m_windowId == windowId && m_phase != AutoAcquisitionRecoveryPhase::Idle) {
        return;
    }
    m_windowId = windowId;
    m_phase = AutoAcquisitionRecoveryPhase::WaitingImmediateScan;
    m_lastScanStartedMs = -1;
    m_lastScanFinishedMs = nowMs;
}

void AutoAcquisitionRecoveryController::leaveWindow()
{
    reset();
}

bool AutoAcquisitionRecoveryController::shouldAttemptScan(const QString& windowId,
                                                          qint64 nowMs,
                                                          int intervalMinutes) const
{
    if (windowId.isEmpty() || windowId != m_windowId) {
        return false;
    }
    if (m_phase == AutoAcquisitionRecoveryPhase::WaitingImmediateScan) {
        return true;
    }
    if (m_phase != AutoAcquisitionRecoveryPhase::WaitingInterval) {
        return false;
    }
    return m_lastScanFinishedMs < 0 ||
           nowMs >= m_lastScanFinishedMs + intervalMsFromMinutes(intervalMinutes);
}

void AutoAcquisitionRecoveryController::noteScanStarted(const QString& windowId,
                                                        qint64 nowMs)
{
    m_windowId = windowId;
    m_phase = AutoAcquisitionRecoveryPhase::Scanning;
    m_lastScanStartedMs = nowMs;
}

void AutoAcquisitionRecoveryController::noteTrackingStarted(const QString& windowId)
{
    m_windowId = windowId;
    m_phase = AutoAcquisitionRecoveryPhase::Tracking;
}

void AutoAcquisitionRecoveryController::noteScanFoundNoStar(qint64 nowMs)
{
    m_phase = AutoAcquisitionRecoveryPhase::WaitingInterval;
    m_lastScanFinishedMs = nowMs;
}

void AutoAcquisitionRecoveryController::noteManualSelectionRequired(qint64 nowMs)
{
    m_phase = AutoAcquisitionRecoveryPhase::AwaitingManualSelection;
    m_lastScanFinishedMs = nowMs;
}

void AutoAcquisitionRecoveryController::noteStarLost(qint64 nowMs)
{
    m_phase = AutoAcquisitionRecoveryPhase::WaitingImmediateScan;
    m_lastScanFinishedMs = nowMs;
}

void AutoAcquisitionRecoveryController::noteManualStop()
{
    reset();
}

qint64 AutoAcquisitionRecoveryController::nextScanDueMs(int intervalMinutes) const
{
    if (m_phase == AutoAcquisitionRecoveryPhase::WaitingImmediateScan) {
        return m_lastScanFinishedMs;
    }
    if (m_lastScanFinishedMs < 0) {
        return -1;
    }
    return m_lastScanFinishedMs + intervalMsFromMinutes(intervalMinutes);
}
