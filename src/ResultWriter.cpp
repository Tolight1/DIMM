#include "ResultWriter.h"

#include <QIODevice>
#include <QStringConverter>

#include <utility>

bool ResultWriter::open(const ResultFileConfig& config, QString* error)
{
    close();
    m_file.setFileName(config.filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = m_file.errorString();
        }
        return false;
    }

    m_filePath = config.filePath;
    if (m_file.write("\xEF\xBB\xBF", 3) != 3) {
        if (error) {
            *error = m_file.errorString();
        }
        m_file.close();
        m_filePath.clear();
        return false;
    }
    m_stream.setDevice(&m_file);
    m_stream.setEncoding(QStringConverter::Utf8);
    if (!config.headerLine.isEmpty()) {
        m_stream << config.headerLine << "\n";
        m_stream.flush();
    }
    return true;
}

void ResultWriter::enqueue(const MeasurementRecord& record)
{
    enqueueLine(record.toCsvLine());
}

void ResultWriter::enqueueLine(const QString& line)
{
    m_pendingLines.append(line);
}

bool ResultWriter::flush(QString* error)
{
    if (error) {
        error->clear();
    }
    if (!m_stream.device()) {
        m_pendingLines.clear();
        return true;
    }

    for (const QString& line : std::as_const(m_pendingLines)) {
        m_stream << line << "\n";
    }
    m_stream.flush();
    const bool success = m_stream.status() == QTextStream::Ok;
    if (!success && error) {
        *error = m_file.errorString();
        if (error->isEmpty()) {
            *error = QStringLiteral("文本流写入失败");
        }
    }
    m_pendingLines.clear();
    return success;
}

bool ResultWriter::close(QString* error)
{
    const bool success = flush(error);
    m_stream.setDevice(nullptr);
    if (m_file.isOpen()) {
        m_file.close();
    }
    m_filePath.clear();
    m_pendingLines.clear();
    return success;
}

bool ResultWriter::isOpen() const
{
    return m_file.isOpen();
}

QString ResultWriter::filePath() const
{
    return m_filePath;
}
