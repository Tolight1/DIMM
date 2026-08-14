#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

#include <cstdint>

class CommManager : public QObject {
    Q_OBJECT
public:
    explicit CommManager(QObject* parent = nullptr);

    void connectToHost(const QString& ip, quint16 port);
    void disconnectFromHost();
    bool isConnected() const;
    bool isConnecting() const;

    void setRemoteAddress(const QString& ip, quint16 port);
    QString remoteAddress() const { return m_remoteIp; }
    quint16 remotePort() const { return m_remotePort; }

    void sendMonitoringFrame(float temperatureC,
                             float humidityRh,
                             float pressureHpa,
                             float r0,
                             float seeing,
                             float theta0,
                             float tau0,
                             float peakBrightnessCameraA,
                             float peakBrightnessCameraB,
                             float exposureTimeCameraAUs,
                             float exposureTimeCameraBUs,
                             float frameRateHz,
                             std::uint32_t deviceStatus,
                             std::uint64_t timestampMs);

signals:
    void connected();
    void disconnected();
    void connectionError(const QString& msg);

private slots:
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError error);
    void onConnectTimeout();

private:
    QTcpSocket* m_socket = nullptr;
    QTimer* m_connectTimer = nullptr;
    QString m_remoteIp = QStringLiteral("169.254.100.2");
    quint16 m_remotePort = 5000;
    std::uint32_t m_sequence = 0;
};
