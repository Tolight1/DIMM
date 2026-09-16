#pragma once

#include "AutoFocusController.h"

#include <array>

#include <QFile>
#include <QString>
#include <QTextStream>

struct AutoFocusLogRecord {
    QString timestamp;
    int sequence = 0;
    QString stage;
    int motorPosition = 0;
    int directionStep = 0;
    AutoFocusMetrics metrics;
    QString statisticsMode;
    bool searchDeadZoneActive = false;
    bool reverseBacklashActive = false;
    int accumulatedBacklashTravel = 0;
    double bestHfr = 0.0;
    AutoFocusReferenceMetrics reference;
    QString finalResult;
};

class AutoFocusLogWriter final {
public:
    ~AutoFocusLogWriter();

    void setEnabled(bool enabled);
    bool append(const QString& sessionDirectory,
                int cameraIndex,
                const AutoFocusLogRecord& record,
                QString* error = nullptr);
    void close();

private:
    bool ensureOpen(const QString& sessionDirectory, int cameraIndex, QString* error);
    static QString headerLine();
    static QString csvField(const QString& value);
    static QString number(double value);

    bool m_enabled = false;
    std::array<QFile, 2> m_files;
    std::array<QTextStream, 2> m_streams;
};
