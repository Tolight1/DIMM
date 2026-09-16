#include "AcquisitionCsv.h"

#include <QDir>
#include <QFileInfo>

#include <cmath>

namespace {

QString csvField(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char(34), QString(2, QLatin1Char(34)));
    if (escaped.contains(QLatin1Char(',')) || escaped.contains(QLatin1Char(34)) ||
        escaped.contains(QLatin1Char('\n')) || escaped.contains(QLatin1Char('\r'))) {
        return QString(1, QLatin1Char(34)) + escaped + QString(1, QLatin1Char(34));
    }
    return escaped;
}

QString csvNumber(double value)
{
    return std::isfinite(value) ? QString::number(value, 'g', 12) : QString();
}

QString formatFields(const QStringList& fields)
{
    QStringList escaped;
    escaped.reserve(fields.size());
    for (const QString& field : fields) {
        escaped.append(csvField(field));
    }
    return escaped.join(QLatin1Char(','));
}

QStringList emptyRecordFields()
{
    return QStringList(AcquisitionCsv::headerColumns().size(), QString());
}

}  // namespace

namespace AcquisitionCsv {

StarTrackingTransition transitionStarTrackingState(StarTrackingState current, bool tracked)
{
    StarTrackingTransition transition;
    transition.nextState = current;
    if (tracked) {
        if (current != StarTrackingState::Tracked) {
            transition.nextState = StarTrackingState::Tracked;
            transition.recordType = QStringLiteral("StarFound");
        }
        return transition;
    }

    if (current == StarTrackingState::Tracked) {
        transition.nextState = StarTrackingState::Lost;
        transition.recordType = QStringLiteral("StarLost");
    }
    return transition;
}

bool allowsData(StarTrackingState state)
{
    return state == StarTrackingState::Tracked;
}

QStringList headerColumns()
{
    return {
        QStringLiteral("timestamp"),
        QStringLiteral("recordType"),
        QStringLiteral("sessionId"),
        QStringLiteral("sourceTimestamp"),
        QStringLiteral("savedAt"),
        QStringLiteral("sourceGeneration"),
        QStringLiteral("temperature"),
        QStringLiteral("humidity"),
        QStringLiteral("pressure"),
        QStringLiteral("r0"),
        QStringLiteral("seeing"),
        QStringLiteral("theta0"),
        QStringLiteral("tau0"),
        QStringLiteral("peakBrightnessA"),
        QStringLiteral("peakBrightnessB"),
        QStringLiteral("exposureTimeA"),
        QStringLiteral("exposureTimeB"),
        QStringLiteral("frameRate"),
        QStringLiteral("deviceStatus"),
        QStringLiteral("acquisitionState"),
        QStringLiteral("searchAttempt"),
        QStringLiteral("reason"),
        QStringLiteral("settingsFile"),
        QStringLiteral("autoFocusHfrA"),
        QStringLiteral("autoFocusRmsA"),
        QStringLiteral("autoFocusHfrB"),
        QStringLiteral("autoFocusRmsB"),
    };
}

QString headerLine()
{
    return formatFields(headerColumns());
}

QString formatTimestamp(const QDateTime& value)
{
    return value.isValid()
               ? value.toString(Qt::ISODateWithMs)
               : QString();
}

QString sessionDirectoryName(AcquisitionCsvSessionType type)
{
    return type == AcquisitionCsvSessionType::Auto
               ? QStringLiteral("auto_acquisition")
               : QStringLiteral("manual_acquisition");
}

QString sessionFileStem(AcquisitionCsvSessionType type, const QDateTime& startedAt)
{
    const QString prefix = type == AcquisitionCsvSessionType::Auto
                                ? QStringLiteral("DIMM_auto")
                                : QStringLiteral("DIMM_manual");
    return QStringLiteral("%1_%2")
        .arg(prefix, startedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss")));
}

QString makeUniqueSessionFilePath(const QString& dataRoot,
                                  AcquisitionCsvSessionType type,
                                  const QDateTime& startedAt)
{
    // A session contains one CSV plus its images/ directory.
    const QString modeDirectory = sessionDirectoryName(type);
    const QString baseStem = sessionFileStem(type, startedAt);
    QDir root(dataRoot);
    QString sessionId = baseStem;
    QString directory = root.filePath(modeDirectory + QLatin1Char('/') + sessionId);
    int suffix = 1;
    while (QFileInfo::exists(directory) ||
           QFileInfo::exists(QDir(directory).filePath(sessionId + QStringLiteral(".csv")))) {
        sessionId = QStringLiteral("%1_%2").arg(baseStem).arg(suffix++, 3, 10, QLatin1Char('0'));
        directory = root.filePath(modeDirectory + QLatin1Char('/') + sessionId);
    }
    return QDir(directory).filePath(sessionId + QStringLiteral(".csv"));
}

QString formatDataRecord(const AcquisitionCsvDataRecord& record)
{
    QStringList fields = emptyRecordFields();
    fields[0] = formatTimestamp(record.timestamp);
    fields[1] = QStringLiteral("Data");
    fields[2] = record.sessionId;
    fields[3] = formatTimestamp(record.sourceTimestamp);
    fields[4] = formatTimestamp(record.savedAt);
    fields[5] = QString::number(record.sourceGeneration);
    fields[6] = csvNumber(record.temperature);
    fields[7] = csvNumber(record.humidity);
    fields[8] = csvNumber(record.pressure);
    fields[9] = csvNumber(record.r0);
    fields[10] = csvNumber(record.seeing);
    fields[11] = csvNumber(record.theta0);
    fields[12] = csvNumber(record.tau0);
    fields[13] = csvNumber(record.peakBrightnessA);
    fields[14] = csvNumber(record.peakBrightnessB);
    fields[15] = csvNumber(record.exposureTimeA);
    fields[16] = csvNumber(record.exposureTimeB);
    fields[17] = csvNumber(record.frameRate);
    fields[18] = record.deviceStatus;
    fields[19] = record.acquisitionState;
    fields[20] = QString::number(record.searchAttempt);
    if (record.hasAutoFocusMetricsA) {
        fields[23] = csvNumber(record.autoFocusHfrA);
        fields[24] = csvNumber(record.autoFocusRmsA);
    }
    if (record.hasAutoFocusMetricsB) {
        fields[25] = csvNumber(record.autoFocusHfrB);
        fields[26] = csvNumber(record.autoFocusRmsB);
    }
    return formatFields(fields);
}

QString formatEventRecord(const QString& recordType,
                          const QString& sessionId,
                          const QDateTime& timestamp,
                          const QDateTime& savedAt,
                          const QDateTime& sourceTimestamp,
                          quint64 sourceGeneration,
                          const QString& acquisitionState,
                          int searchAttempt,
                          const QString& reason)
{
    return formatEventRecord(recordType,
                             sessionId,
                             timestamp,
                             savedAt,
                             sourceTimestamp,
                             sourceGeneration,
                             0u,
                             acquisitionState,
                             searchAttempt,
                             reason);
}

QString formatEventRecord(const QString& recordType,
                          const QString& sessionId,
                          const QDateTime& timestamp,
                          const QDateTime& savedAt,
                          const QDateTime& sourceTimestamp,
                          quint64 sourceGeneration,
                          std::uint32_t deviceStatus,
                          const QString& acquisitionState,
                          int searchAttempt,
                          const QString& reason)
{
    QStringList fields = emptyRecordFields();
    fields[0] = formatTimestamp(timestamp);
    fields[1] = recordType;
    fields[2] = sessionId;
    fields[3] = formatTimestamp(sourceTimestamp);
    fields[4] = formatTimestamp(savedAt);
    fields[5] = QString::number(sourceGeneration);
    fields[18] = QString::number(deviceStatus);
    fields[19] = acquisitionState;
    fields[20] = QString::number(searchAttempt);
    fields[21] = reason;
    return formatFields(fields);
}

QString formatSettingsRecord(const QString& recordType,
                             const QString& sessionId,
                             const QDateTime& timestamp,
                             const QDateTime& savedAt,
                             quint64 sourceGeneration,
                             const QString& acquisitionState,
                             int searchAttempt,
                             const QString& settingsFile,
                             const QString& reason)
{
    QStringList fields = emptyRecordFields();
    fields[0] = formatTimestamp(timestamp);
    fields[1] = recordType;
    fields[2] = sessionId;
    fields[3] = formatTimestamp(savedAt);
    fields[4] = formatTimestamp(savedAt);
    fields[5] = QString::number(sourceGeneration);
    fields[19] = acquisitionState;
    fields[20] = QString::number(searchAttempt);
    fields[21] = reason;
    fields[22] = settingsFile;
    return formatFields(fields);
}

}  // namespace AcquisitionCsv
