#include "ResultWriter.h"

#include <QIODevice>

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
    m_stream.setDevice(&m_file);
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

void ResultWriter::flush()
{
    if (!m_stream.device()) {
        m_pendingLines.clear();
        return;
    }

    for (const QString& line : std::as_const(m_pendingLines)) {
        m_stream << line << "\n";
    }
    m_stream.flush();
    m_pendingLines.clear();
}

void ResultWriter::close()
{
    flush();
    m_stream.setDevice(nullptr);
    if (m_file.isOpen()) {
        m_file.close();
    }
    m_filePath.clear();
    m_pendingLines.clear();
}

bool ResultWriter::isOpen() const
{
    return m_file.isOpen();
}

QString ResultWriter::filePath() const
{
    return m_filePath;
}
