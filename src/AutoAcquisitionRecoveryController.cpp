#include "AutoAcquisitionRecoveryController.h"

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
    Q_UNUSED(intervalMinutes);
    if (windowId.isEmpty() || windowId != m_windowId) {
        return false;
    }
    Q_UNUSED(nowMs);
    return m_phase == AutoAcquisitionRecoveryPhase::WaitingImmediateScan;
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
    m_phase = AutoAcquisitionRecoveryPhase::WaitingImmediateScan;
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
    Q_UNUSED(intervalMinutes);
    return m_phase == AutoAcquisitionRecoveryPhase::WaitingImmediateScan
               ? m_lastScanFinishedMs
               : -1;
}
