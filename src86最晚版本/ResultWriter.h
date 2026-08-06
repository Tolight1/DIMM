#pragma once

#include <QChar>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTextStream>

struct ResultFileConfig {
    QString filePath;
    QString headerLine;
};

struct MeasurementRecord {
    QStringList fields;

    QString toCsvLine() const
    {
        return fields.join(QLatin1Char(','));
    }
};

class ResultWriter final {
public:
    bool open(const ResultFileConfig& config, QString* error = nullptr);
    void enqueue(const MeasurementRecord& record);
    void enqueueLine(const QString& line);
    void flush();
    void close();

    bool isOpen() const;
    QString filePath() const;

private:
    QFile m_file;
    QTextStream m_stream;
    QString m_filePath;
    QStringList m_pendingLines;
};
