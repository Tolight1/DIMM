#pragma once

#include <QtGlobal>
#include <QString>

enum class AutoAcquisitionRecoveryPhase {
    Idle,
    WaitingImmediateScan,
    Scanning,
    Tracking,
    WaitingInterval,
    AwaitingManualSelection
};

class AutoAcquisitionRecoveryController {
public:
    void reset();
    void enterWindow(const QString& windowId, qint64 nowMs);
    void leaveWindow();
    bool shouldAttemptScan(const QString& windowId,
                           qint64 nowMs,
                           int intervalMinutes) const;
    void noteScanStarted(const QString& windowId, qint64 nowMs);
    void noteTrackingStarted(const QString& windowId);
    void noteScanFoundNoStar(qint64 nowMs);
    void noteManualSelectionRequired(qint64 nowMs);
    void noteStarLost(qint64 nowMs);
    void noteManualStop();

    AutoAcquisitionRecoveryPhase phase() const { return m_phase; }
    QString windowId() const { return m_windowId; }
    qint64 lastScanFinishedMs() const { return m_lastScanFinishedMs; }
    qint64 nextScanDueMs(int intervalMinutes) const;

private:
    AutoAcquisitionRecoveryPhase m_phase = AutoAcquisitionRecoveryPhase::Idle;
    QString m_windowId;
    qint64 m_lastScanStartedMs = -1;
    qint64 m_lastScanFinishedMs = -1;
};
