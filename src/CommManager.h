#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QTimer>
#include <QHostAddress>

// ============================================================
// 通信协议常量
// ============================================================
namespace CommProtocol {
    constexpr uint16_t FRAME_HEADER = 0xAA55;
    constexpr int HEADER_SIZE = 2;
    constexpr int LENGTH_SIZE = 2;
    constexpr int CMD_SIZE = 1;
    constexpr int CHECKSUM_SIZE = 1;
    constexpr int MIN_FRAME_SIZE = HEADER_SIZE + LENGTH_SIZE + CMD_SIZE + CHECKSUM_SIZE; // 6

    // 命令码：上位机 → 设备
    constexpr uint8_t CMD_START_REPORT  = 0x01; // 开始发送数据
    constexpr uint8_t CMD_STOP_REPORT   = 0x02; // 停止发送数据
    constexpr uint8_t CMD_QUERY_STATUS  = 0x03; // 查询设备状态

    // 命令码：设备 → 上位机
    constexpr uint8_t CMD_MEASUREMENT   = 0x81; // 测量结果上报
    constexpr uint8_t CMD_DEVICE_STATUS = 0x82; // 设备状态上报
    constexpr uint8_t CMD_ACK           = 0x83; // 应答
}

// ============================================================
// 通信管理器：TCP客户端，与上位机通信
// ============================================================
class CommManager : public QObject {
    Q_OBJECT
public:
    explicit CommManager(QObject* parent = nullptr);

    // 连接管理
    void connectToHost(const QString& ip, quint16 port);
    void disconnectFromHost();
    bool isConnected() const;

    // 配置
    void setRemoteAddress(const QString& ip, quint16 port);
    QString remoteAddress() const { return m_remoteIp; }
    quint16 remotePort() const { return m_remotePort; }

    // 发送数据
    void sendMeasurement(double r0, double seeing, double theta0, double tau0,
                         double cx, double cy, int camIdx, uint32_t frameCount);
    void sendDeviceStatus(float temp, float fps, bool cam0Connected, bool cam1Connected,
                          bool isCapturing, uint32_t uptimeMs);
    void sendAck(uint8_t ackCmd, uint8_t status = 0);

signals:
    void connected();
    void disconnected();
    void connectionError(const QString& msg);
    void commandReceived(uint8_t cmd); // 收到上位机指令

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);

private:
    QTcpSocket* m_socket = nullptr;
    QString m_remoteIp = "192.168.1.100";
    quint16 m_remotePort = 5000;

    QByteArray m_recvBuffer; // TCP粘包处理缓冲区

    // 协议工具
    QByteArray buildFrame(uint8_t cmd, const QByteArray& data = QByteArray());
    void processBuffer();
    static uint8_t calcXorChecksum(const QByteArray& data);
};
