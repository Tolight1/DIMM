#include "CommManager.h"
#include <QDebug>
#include <QDateTime>
#include <cstring>

CommManager::CommManager(QObject* parent)
    : QObject(parent)
{
    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::connected, this, &CommManager::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &CommManager::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &CommManager::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &CommManager::onError);
}

// ============================================================
// 连接管理
// ============================================================

void CommManager::connectToHost(const QString& ip, quint16 port)
{
    m_remoteIp = ip;
    m_remotePort = port;

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }

    qDebug() << "[CommManager] 正在连接" << ip << ":" << port;
    m_socket->connectToHost(QHostAddress(ip), port);
}

void CommManager::disconnectFromHost()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

bool CommManager::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void CommManager::setRemoteAddress(const QString& ip, quint16 port)
{
    m_remoteIp = ip;
    m_remotePort = port;
}

// ============================================================
// 槽函数
// ============================================================

void CommManager::onConnected()
{
    qDebug() << "[CommManager] 已连接到" << m_remoteIp << ":" << m_remotePort;
    m_recvBuffer.clear();
    emit connected();
}

void CommManager::onDisconnected()
{
    qDebug() << "[CommManager] 已断开连接";
    m_recvBuffer.clear();
    emit disconnected();
}

void CommManager::onReadyRead()
{
    m_recvBuffer.append(m_socket->readAll());
    processBuffer();
}

void CommManager::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    QString msg = m_socket->errorString();
    qDebug() << "[CommManager] 连接错误:" << msg;
    emit connectionError(msg);
}

// ============================================================
// 协议：构建帧
// ============================================================

QByteArray CommManager::buildFrame(uint8_t cmd, const QByteArray& data)
{
    QByteArray frame;
    frame.resize(CommProtocol::HEADER_SIZE + CommProtocol::LENGTH_SIZE +
                 CommProtocol::CMD_SIZE + data.size() + CommProtocol::CHECKSUM_SIZE);

    int offset = 0;

    // 帧头 0xAA55
    frame[offset++] = static_cast<char>((CommProtocol::FRAME_HEADER >> 8) & 0xFF);
    frame[offset++] = static_cast<char>(CommProtocol::FRAME_HEADER & 0xFF);

    // 长度（命令码+数据的字节数）
    uint16_t payloadLen = CommProtocol::CMD_SIZE + data.size();
    frame[offset++] = (payloadLen >> 8) & 0xFF;
    frame[offset++] = payloadLen & 0xFF;

    // 命令码
    frame[offset++] = cmd;

    // 数据
    if (!data.isEmpty()) {
        memcpy(frame.data() + offset, data.constData(), data.size());
        offset += data.size();
    }

    // XOR校验（对帧头之后、校验之前的所有字节）
    QByteArray checkData = frame.mid(CommProtocol::HEADER_SIZE,
                                      frame.size() - CommProtocol::HEADER_SIZE - 1);
    frame[offset] = calcXorChecksum(checkData);

    return frame;
}

uint8_t CommManager::calcXorChecksum(const QByteArray& data)
{
    uint8_t checksum = 0;
    for (char c : data) {
        checksum ^= static_cast<uint8_t>(c);
    }
    return checksum;
}

// ============================================================
// 协议：解析缓冲区
// ============================================================

void CommManager::processBuffer()
{
    while (m_recvBuffer.size() >= CommProtocol::MIN_FRAME_SIZE) {
        // 查找帧头
        int headerPos = -1;
        for (int i = 0; i <= m_recvBuffer.size() - CommProtocol::HEADER_SIZE; ++i) {
            uint8_t high = static_cast<uint8_t>(m_recvBuffer[i]);
            uint8_t low = static_cast<uint8_t>(m_recvBuffer[i + 1]);
            if (high == 0xAA && low == 0x55) {
                headerPos = i;
                break;
            }
        }

        if (headerPos < 0) {
            // 没找到帧头，丢弃所有数据
            m_recvBuffer.clear();
            return;
        }

        // 丢弃帧头之前的垃圾数据
        if (headerPos > 0) {
            m_recvBuffer = m_recvBuffer.mid(headerPos);
        }

        // 检查是否有完整帧
        if (m_recvBuffer.size() < CommProtocol::MIN_FRAME_SIZE) return;

        // 读取长度
        uint16_t payloadLen = (static_cast<uint8_t>(m_recvBuffer[2]) << 8) |
                               static_cast<uint8_t>(m_recvBuffer[3]);
        int totalFrameSize = CommProtocol::HEADER_SIZE + CommProtocol::LENGTH_SIZE +
                             payloadLen + CommProtocol::CHECKSUM_SIZE;

        if (m_recvBuffer.size() < totalFrameSize) return; // 数据不完整，等下次

        // 提取完整帧
        QByteArray frame = m_recvBuffer.left(totalFrameSize);
        m_recvBuffer = m_recvBuffer.mid(totalFrameSize);

        // 校验
        QByteArray checkData = frame.mid(CommProtocol::HEADER_SIZE,
                                          totalFrameSize - CommProtocol::HEADER_SIZE - 1);
        uint8_t receivedCs = static_cast<uint8_t>(frame[totalFrameSize - 1]);
        uint8_t calcCs = calcXorChecksum(checkData);

        if (receivedCs != calcCs) {
            qDebug() << "[CommManager] 校验失败，丢弃帧";
            continue;
        }

        // 解析命令码
        uint8_t cmd = static_cast<uint8_t>(frame[4]);
        qDebug() << "[CommManager] 收到命令: 0x" << QString::number(cmd, 16);
        emit commandReceived(cmd);
    }
}

// ============================================================
// 发送：测量结果 (0x81)
// ============================================================

void CommManager::sendMeasurement(double r0, double seeing, double theta0, double tau0,
                                   double cx, double cy, int camIdx, uint32_t frameCount)
{
    if (!isConnected()) return;

    QByteArray data;
    data.resize(37);
    int offset = 0;

    // 时间戳 (uint64 ms)
    uint64_t timestamp = QDateTime::currentMSecsSinceEpoch();
    memcpy(data.data() + offset, &timestamp, 8);
    offset += 8;

    // r0, seeing, theta0, tau0 (float)
    float f;
    f = static_cast<float>(r0);     memcpy(data.data() + offset, &f, 4); offset += 4;
    f = static_cast<float>(seeing); memcpy(data.data() + offset, &f, 4); offset += 4;
    f = static_cast<float>(theta0); memcpy(data.data() + offset, &f, 4); offset += 4;
    f = static_cast<float>(tau0);   memcpy(data.data() + offset, &f, 4); offset += 4;

    // centroidX, centroidY (float)
    f = static_cast<float>(cx); memcpy(data.data() + offset, &f, 4); offset += 4;
    f = static_cast<float>(cy); memcpy(data.data() + offset, &f, 4); offset += 4;

    // cameraIndex (uint8)
    data[offset++] = static_cast<char>(camIdx);

    // frameCount (uint32)
    memcpy(data.data() + offset, &frameCount, 4);

    QByteArray frame = buildFrame(CommProtocol::CMD_MEASUREMENT, data);
    m_socket->write(frame);
}

// ============================================================
// 发送：设备状态 (0x82)
// ============================================================

void CommManager::sendDeviceStatus(float temp, float fps, bool cam0Connected, bool cam1Connected,
                                    bool isCapturing, uint32_t uptimeMs)
{
    if (!isConnected()) return;

    QByteArray data;
    data.resize(15);
    int offset = 0;

    float f;
    f = temp; memcpy(data.data() + offset, &f, 4); offset += 4;
    f = fps;  memcpy(data.data() + offset, &f, 4); offset += 4;

    data[offset++] = cam0Connected ? 1 : 0;
    data[offset++] = cam1Connected ? 1 : 0;
    data[offset++] = isCapturing ? 1 : 0;

    memcpy(data.data() + offset, &uptimeMs, 4);

    QByteArray frame = buildFrame(CommProtocol::CMD_DEVICE_STATUS, data);
    m_socket->write(frame);
}

// ============================================================
// 发送：应答 (0x83)
// ============================================================

void CommManager::sendAck(uint8_t ackCmd, uint8_t status)
{
    if (!isConnected()) return;

    QByteArray data;
    data.append(static_cast<char>(ackCmd));
    data.append(static_cast<char>(status));

    QByteArray frame = buildFrame(CommProtocol::CMD_ACK, data);
    m_socket->write(frame);
}
