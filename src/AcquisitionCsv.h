#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <cstdint>

enum class AcquisitionCsvSessionType {
    Auto,
    Manual
};

enum class StarTrackingState {
    Unknown,
    Tracked,
    Lost
};

struct StarTrackingTransition {
    StarTrackingState nextState = StarTrackingState::Unknown;
    QString recordType;
};

struct AcquisitionCsvDataRecord {
    QDateTime timestamp;
    QString sessionId;
    QDateTime sourceTimestamp;
    QDateTime savedAt;
    // Identifies the acquisition generation that produced the asynchronous result.
    quint64 sourceGeneration = 0;
    double temperature = 0.0;
    double humidity = 0.0;
    double pressure = 0.0;
    double r0 = 0.0;
    double seeing = 0.0;
    double theta0 = 0.0;
    double tau0 = 0.0;
    double peakBrightnessA = 0.0;
    double peakBrightnessB = 0.0;
    double exposureTimeA = 0.0;
    double exposureTimeB = 0.0;
    double frameRate = 0.0;
    QString deviceStatus;
    QString acquisitionState;
    int searchAttempt = 0;
    double autoFocusHfrA = 0.0;
    double autoFocusRmsA = 0.0;
    bool hasAutoFocusMetricsA = false;
    double autoFocusHfrB = 0.0;
    double autoFocusRmsB = 0.0;
    bool hasAutoFocusMetricsB = false;
};

namespace AcquisitionCsv {

StarTrackingTransition transitionStarTrackingState(StarTrackingState current, bool tracked);
bool allowsData(StarTrackingState state);
QStringList headerColumns();
QString headerLine();
QString sessionDirectoryName(AcquisitionCsvSessionType type);
QString sessionFileStem(AcquisitionCsvSessionType type, const QDateTime& startedAt);
QString makeUniqueSessionFilePath(const QString& dataRoot,
                                  AcquisitionCsvSessionType type,
                                  const QDateTime& startedAt);
QString formatTimestamp(const QDateTime& value);
QString formatDataRecord(const AcquisitionCsvDataRecord& record);
QString formatEventRecord(const QString& recordType,
                          const QString& sessionId,
                          const QDateTime& timestamp,
                          const QDateTime& savedAt,
                          const QDateTime& sourceTimestamp,
                          quint64 sourceGeneration,
                          const QString& acquisitionState,
                          int searchAttempt,
                          const QString& reason);
QString formatEventRecord(const QString& recordType,
                          const QString& sessionId,
                          const QDateTime& timestamp,
                          const QDateTime& savedAt,
                          const QDateTime& sourceTimestamp,
                          quint64 sourceGeneration,
                          std::uint32_t deviceStatus,
                          const QString& acquisitionState,
                          int searchAttempt,
                          const QString& reason);
QString formatSettingsRecord(const QString& recordType,
                             const QString& sessionId,
                             const QDateTime& timestamp,
                             const QDateTime& savedAt,
                             quint64 sourceGeneration,
                             const QString& acquisitionState,
                             int searchAttempt,
                             const QString& settingsFile,
                             const QString& reason = QString());

}  // namespace AcquisitionCsv
