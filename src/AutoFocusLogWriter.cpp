#include "AutoFocusLogWriter.h"

#include <QDir>
#include <QIODevice>
#include <QStringConverter>

#include <cmath>

AutoFocusLogWriter::~AutoFocusLogWriter()
{
    close();
}

void AutoFocusLogWriter::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (!m_enabled) {
        close();
    }
}

bool AutoFocusLogWriter::append(const QString& sessionDirectory,
                                int cameraIndex,
                                const AutoFocusLogRecord& record,
                                QString* error)
{
    if (error) {
        error->clear();
    }
    if (!m_enabled) {
        return true;
    }
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(m_files.size()) ||
        sessionDirectory.isEmpty()) {
        if (error) {
            *error = QStringLiteral("自动调焦日志目标无效");
        }
        return false;
    }
    if (!ensureOpen(sessionDirectory, cameraIndex, error)) {
        return false;
    }

    const QStringList fields = {
        record.timestamp,
        QString::number(record.sequence),
        QString::number(cameraIndex + 1),
        record.stage,
        QString::number(record.motorPosition),
        record.directionStep > 0 ? QStringLiteral("+%1").arg(record.directionStep)
                                 : QString::number(record.directionStep),
        number(record.metrics.hfr),
        number(record.metrics.rms),
        number(record.metrics.hfrStandardDeviation),
        number(record.metrics.rmsStandardDeviation),
        record.statisticsMode,
        QString::number(record.metrics.rawFrameCount),
        QString::number(record.metrics.validFrameCount),
        record.searchDeadZoneActive ? QStringLiteral("1") : QStringLiteral("0"),
        record.reverseBacklashActive ? QStringLiteral("1") : QStringLiteral("0"),
        QString::number(record.accumulatedBacklashTravel),
        number(record.bestHfr),
        number(record.reference.hfr),
        number(record.reference.rms),
        record.finalResult
    };
    QStringList escaped;
    escaped.reserve(fields.size());
    for (const QString& field : fields) {
        escaped.append(csvField(field));
    }
    QTextStream& stream = m_streams[cameraIndex];
    stream << escaped.join(QLatin1Char(',')) << '\n';
    stream.flush();
    if (stream.status() != QTextStream::Ok) {
        if (error) {
            *error = m_files[cameraIndex].errorString();
            if (error->isEmpty()) {
                *error = QStringLiteral("自动调焦日志写入失败");
            }
        }
        return false;
    }
    return true;
}

void AutoFocusLogWriter::close()
{
    for (int camera = 0; camera < static_cast<int>(m_files.size()); ++camera) {
        m_streams[camera].flush();
        m_streams[camera].setDevice(nullptr);
        if (m_files[camera].isOpen()) {
            m_files[camera].close();
        }
    }
}

bool AutoFocusLogWriter::ensureOpen(const QString& sessionDirectory,
                                    int cameraIndex,
                                    QString* error)
{
    const QDir sessionDir(sessionDirectory);
    const QString legacyPath =
        sessionDir.filePath(QStringLiteral("autofocus_camera%1.csv").arg(cameraIndex + 1));
    QString filePath = legacyPath;
    if (QFileInfo::exists(legacyPath) && QFileInfo(legacyPath).size() > 0) {
        QFile legacyFile(legacyPath);
        if (legacyFile.open(QIODevice::ReadOnly | QIODevice::Text) &&
            QString::fromUtf8(legacyFile.readLine()).trimmed() != headerLine()) {
            filePath = sessionDir.filePath(
                QStringLiteral("autofocus_camera%1_hfr_rms.csv").arg(cameraIndex + 1));
        }
    }
    QFile& file = m_files[cameraIndex];
    QTextStream& stream = m_streams[cameraIndex];
    if (file.isOpen() && file.fileName() == filePath) {
        return true;
    }

    stream.flush();
    stream.setDevice(nullptr);
    if (file.isOpen()) {
        file.close();
    }

    const bool needsHeader = !QFile::exists(filePath) || QFileInfo(filePath).size() == 0;
    file.setFileName(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    stream.setDevice(&file);
    stream.setEncoding(QStringConverter::Utf8);
    if (needsHeader) {
        stream << headerLine() << '\n';
        stream.flush();
        if (stream.status() != QTextStream::Ok) {
            if (error) {
                *error = file.errorString();
            }
            return false;
        }
    }
    return true;
}

QString AutoFocusLogWriter::headerLine()
{
    return QStringLiteral(
        "Timestamp,Sequence,Camera,Stage,MotorPosition,DirectionStep,"
        "HFR,RMS,HFRStdDev,RMSStdDev,StatisticsMode,"
        "RawFrameCount,ValidFrameCount,SearchDeadZoneActive,ReverseBacklashActive,"
        "AccumulatedBacklashTravel,BestHFR,ReferenceHFR,ReferenceRMS,"
        "FinalResult");
}

QString AutoFocusLogWriter::csvField(const QString& value)
{
    const bool quoted = value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"')) ||
                        value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'));
    if (!quoted) {
        return value;
    }
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString AutoFocusLogWriter::number(double value)
{
    return std::isfinite(value) ? QString::number(value, 'g', 12) : QString();
}
