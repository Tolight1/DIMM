#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>

namespace CommProtocol {
constexpr uint16_t FRAME_HEADER = 0xAA55;
constexpr int HEADER_SIZE = 2;
constexpr int LENGTH_SIZE = 2;
constexpr int CMD_SIZE = 1;
constexpr int CHECKSUM_SIZE = 1;
constexpr int MIN_FRAME_SIZE = HEADER_SIZE + LENGTH_SIZE + CMD_SIZE + CHECKSUM_SIZE;

constexpr uint8_t CMD_START_REPORT = 0x01;
constexpr uint8_t CMD_STOP_REPORT = 0x02;
constexpr uint8_t CMD_QUERY_STATUS = 0x03;
constexpr uint8_t CMD_MEASUREMENT = 0x81;
constexpr uint8_t CMD_DEVICE_STATUS = 0x82;
constexpr uint8_t CMD_ACK = 0x83;

constexpr int MEASUREMENT_PAYLOAD_SIZE = 84;
constexpr int DEVICE_STATUS_PAYLOAD_SIZE = 15;
}

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

    void sendMeasurement(double r0,
                         double seeing,
                         double theta0,
                         double tau0,
                         double centroid1X,
                         double centroid1Y,
                         double centroid2X,
                         double centroid2Y,
                         double peak1,
                         double peak2,
                         int roi1X,
                         int roi1Y,
                         int roi1W,
                         int roi1H,
                         int roi2X,
                         int roi2Y,
                         int roi2W,
                         int roi2H,
                         uint32_t frameCount);
    void sendDeviceStatus(float temp, float fps, bool cam0Connected, bool cam1Connected,
                          bool isCapturing, uint32_t uptimeMs);
    void sendAck(uint8_t ackCmd, uint8_t status = 0);

signals:
    void connected();
    void disconnected();
    void connectionError(const QString& msg);
    void commandReceived(uint8_t cmd);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);
    void onConnectTimeout();

private:
    QByteArray buildFrame(uint8_t cmd, const QByteArray& data = QByteArray());
    void processBuffer();
    static uint8_t calcXorChecksum(const QByteArray& data);

    QTcpSocket* m_socket = nullptr;
    QTimer* m_connectTimer = nullptr;
    QString m_remoteIp = QStringLiteral("192.168.10.1");
    quint16 m_remotePort = 5000;
    QByteArray m_recvBuffer;
};
