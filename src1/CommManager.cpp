#include "CommManager.h"

#include <QDateTime>
#include <QDebug>
#include <QNetworkProxy>

#include <cstdint>
#include <cstring>

namespace {
static_assert(sizeof(uint64_t) + 10 * sizeof(float) + 8 * sizeof(int32_t) + sizeof(uint32_t) ==
                  CommProtocol::MEASUREMENT_PAYLOAD_SIZE,
              "Measurement payload layout must remain 84 bytes.");

static_assert(2 * sizeof(float) + 3 * sizeof(uint8_t) + sizeof(uint32_t) ==
                  CommProtocol::DEVICE_STATUS_PAYLOAD_SIZE,
              "Device status payload layout must remain 15 bytes.");

QString socketErrorText(QAbstractSocket::SocketError error, const QString& fallback)
{
    switch (error) {
    case QAbstractSocket::ConnectionRefusedError:
        return QStringLiteral("\u8fde\u63a5\u88ab\u62d2\u7edd\uff0c\u8bf7\u68c0\u67e5\u76ee\u6807\u4e3b\u673a\u548c\u7aef\u53e3\u662f\u5426\u5df2\u5f00\u542f\u3002");
    case QAbstractSocket::RemoteHostClosedError:
        return QStringLiteral("\u8fdc\u7aef\u4e3b\u673a\u5df2\u65ad\u5f00\u8fde\u63a5\u3002");
    case QAbstractSocket::HostNotFoundError:
        return QStringLiteral("\u672a\u627e\u5230\u76ee\u6807\u4e3b\u673a\uff0c\u8bf7\u68c0\u67e5 IP \u5730\u5740\u3002");
    case QAbstractSocket::SocketAccessError:
        return QStringLiteral("\u5957\u63a5\u5b57\u8bbf\u95ee\u88ab\u62d2\u7edd\uff0c\u8bf7\u68c0\u67e5\u7cfb\u7edf\u6743\u9650\u6216\u9632\u706b\u5899\u8bbe\u7f6e\u3002");
    case QAbstractSocket::SocketResourceError:
        return QStringLiteral("\u672c\u673a\u5957\u63a5\u5b57\u8d44\u6e90\u4e0d\u8db3\u3002");
    case QAbstractSocket::SocketTimeoutError:
        return QStringLiteral("\u7f51\u7edc\u901a\u4fe1\u8d85\u65f6\u3002");
    case QAbstractSocket::DatagramTooLargeError:
        return QStringLiteral("\u6570\u636e\u62a5\u6587\u8fc7\u5927\u3002");
    case QAbstractSocket::NetworkError:
        return QStringLiteral("\u7f51\u7edc\u5f02\u5e38\uff0c\u8bf7\u68c0\u67e5\u7f51\u7ebf\u6216\u7f51\u7edc\u914d\u7f6e\u3002");
    case QAbstractSocket::AddressInUseError:
        return QStringLiteral("\u672c\u5730\u5730\u5740\u6216\u7aef\u53e3\u5df2\u88ab\u5360\u7528\u3002");
    case QAbstractSocket::SocketAddressNotAvailableError:
        return QStringLiteral("\u672c\u5730\u5730\u5740\u4e0d\u53ef\u7528\u3002");
    case QAbstractSocket::UnsupportedSocketOperationError:
        return QStringLiteral("\u5f53\u524d\u7cfb\u7edf\u4e0d\u652f\u6301\u8be5\u5957\u63a5\u5b57\u64cd\u4f5c\u3002");
    case QAbstractSocket::ProxyAuthenticationRequiredError:
        return QStringLiteral("\u4ee3\u7406\u8ba4\u8bc1\u5931\u8d25\u6216\u672a\u63d0\u4f9b\u8ba4\u8bc1\u4fe1\u606f\u3002");
    case QAbstractSocket::SslHandshakeFailedError:
        return QStringLiteral("SSL \u63e1\u624b\u5931\u8d25\u3002");
    case QAbstractSocket::ProxyConnectionRefusedError:
        return QStringLiteral("\u4ee3\u7406\u670d\u52a1\u5668\u62d2\u7edd\u8fde\u63a5\u3002");
    case QAbstractSocket::ProxyConnectionClosedError:
        return QStringLiteral("\u4ee3\u7406\u670d\u52a1\u5668\u5df2\u5173\u95ed\u8fde\u63a5\u3002");
    case QAbstractSocket::ProxyConnectionTimeoutError:
        return QStringLiteral("\u8fde\u63a5\u4ee3\u7406\u670d\u52a1\u5668\u8d85\u65f6\u3002");
    case QAbstractSocket::ProxyNotFoundError:
        return QStringLiteral("\u672a\u627e\u5230\u4ee3\u7406\u670d\u52a1\u5668\u3002");
    case QAbstractSocket::ProxyProtocolError:
        return QStringLiteral("\u4ee3\u7406\u534f\u8bae\u9519\u8bef\u3002");
    case QAbstractSocket::OperationError:
        return QStringLiteral("\u5f53\u524d\u8fde\u63a5\u72b6\u6001\u4e0b\u65e0\u6cd5\u6267\u884c\u8be5\u901a\u4fe1\u64cd\u4f5c\u3002");
    case QAbstractSocket::SslInternalError:
        return QStringLiteral("SSL \u5185\u90e8\u9519\u8bef\u3002");
    case QAbstractSocket::SslInvalidUserDataError:
        return QStringLiteral("SSL \u7528\u6237\u6570\u636e\u65e0\u6548\u3002");
    case QAbstractSocket::TemporaryError:
        return QStringLiteral("\u901a\u4fe1\u6682\u65f6\u5931\u8d25\uff0c\u8bf7\u7a0d\u540e\u91cd\u8bd5\u3002");
    case QAbstractSocket::UnknownSocketError:
    default:
        return fallback.isEmpty() ? QStringLiteral("\u672a\u77e5\u901a\u4fe1\u9519\u8bef\u3002") : fallback;
    }
}
}

CommManager::CommManager(QObject* parent)
    : QObject(parent)
{
    m_socket = new QTcpSocket(this);
    m_socket->setProxy(QNetworkProxy::NoProxy);
    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);

    connect(m_socket, &QTcpSocket::connected, this, &CommManager::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &CommManager::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &CommManager::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &CommManager::onError);
    connect(m_connectTimer, &QTimer::timeout, this, &CommManager::onConnectTimeout);
}

void CommManager::connectToHost(const QString& ip, quint16 port)
{
    QHostAddress address;
    if (!address.setAddress(ip) || port == 0) {
        emit connectionError(QStringLiteral("\u65e0\u6548\u7684\u7f51\u7edc\u5730\u5740\u6216\u7aef\u53e3\u3002"));
        return;
    }

    const bool sameEndpoint = (m_remoteIp == ip && m_remotePort == port);
    if (sameEndpoint && m_socket->state() == QAbstractSocket::ConnectedState) {
        return;
    }

    m_remoteIp = ip;
    m_remotePort = port;

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }

    qDebug() << "[CommManager] Connecting to" << ip << ":" << port;
    m_socket->connectToHost(address, port);
    m_connectTimer->start(3000);
}

void CommManager::disconnectFromHost()
{
    if (m_connectTimer->isActive()) {
        m_connectTimer->stop();
    }
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
}

bool CommManager::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

bool CommManager::isConnecting() const
{
    return m_socket->state() == QAbstractSocket::ConnectingState;
}

void CommManager::setRemoteAddress(const QString& ip, quint16 port)
{
    m_remoteIp = ip;
    m_remotePort = port;
}

void CommManager::onConnected()
{
    if (m_connectTimer->isActive()) {
        m_connectTimer->stop();
    }
    qDebug() << "[CommManager] Connected to" << m_remoteIp << ":" << m_remotePort;
    m_recvBuffer.clear();
    emit connected();
}

void CommManager::onDisconnected()
{
    if (m_connectTimer->isActive()) {
        m_connectTimer->stop();
    }
    qDebug() << "[CommManager] Disconnected";
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
    if (m_connectTimer->isActive()) {
        m_connectTimer->stop();
    }
    const QString rawMessage = m_socket->errorString();
    const QString userMessage = socketErrorText(error, rawMessage);
    qDebug() << "[CommManager] Socket error:" << rawMessage;
    emit connectionError(userMessage);
}

void CommManager::onConnectTimeout()
{
    if (m_socket->state() == QAbstractSocket::ConnectingState) {
        qDebug() << "[CommManager] Connect timeout:" << m_remoteIp << ":" << m_remotePort;
        m_socket->abort();
        emit connectionError(QStringLiteral("\u8fde\u63a5\u8d85\u65f6\uff0c\u8bf7\u68c0\u67e5\u4e0a\u4f4d\u673a IP\u3001\u7aef\u53e3\u548c\u9632\u706b\u5899\u8bbe\u7f6e\u3002"));
    }
}

QByteArray CommManager::buildFrame(uint8_t cmd, const QByteArray& data)
{
    QByteArray frame;
    frame.resize(CommProtocol::HEADER_SIZE + CommProtocol::LENGTH_SIZE +
                 CommProtocol::CMD_SIZE + data.size() + CommProtocol::CHECKSUM_SIZE);

    int offset = 0;
    frame[offset++] = static_cast<char>((CommProtocol::FRAME_HEADER >> 8) & 0xFF);
    frame[offset++] = static_cast<char>(CommProtocol::FRAME_HEADER & 0xFF);

    const uint16_t payloadLen = static_cast<uint16_t>(CommProtocol::CMD_SIZE + data.size());
    frame[offset++] = static_cast<char>((payloadLen >> 8) & 0xFF);
    frame[offset++] = static_cast<char>(payloadLen & 0xFF);
    frame[offset++] = static_cast<char>(cmd);

    if (!data.isEmpty()) {
        std::memcpy(frame.data() + offset, data.constData(), static_cast<size_t>(data.size()));
        offset += data.size();
    }

    const QByteArray checkData = frame.mid(CommProtocol::HEADER_SIZE,
                                           frame.size() - CommProtocol::HEADER_SIZE - 1);
    frame[offset] = static_cast<char>(calcXorChecksum(checkData));
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

void CommManager::processBuffer()
{
    while (m_recvBuffer.size() >= CommProtocol::MIN_FRAME_SIZE) {
        int headerPos = -1;
        for (int i = 0; i <= m_recvBuffer.size() - CommProtocol::HEADER_SIZE; ++i) {
            const uint8_t high = static_cast<uint8_t>(m_recvBuffer[i]);
            const uint8_t low = static_cast<uint8_t>(m_recvBuffer[i + 1]);
            if (high == 0xAA && low == 0x55) {
                headerPos = i;
                break;
            }
        }

        if (headerPos < 0) {
            m_recvBuffer.clear();
            qDebug() << "[CommManager] Invalid frame header, buffer cleared";
            continue;
        }

        if (headerPos > 0) {
            m_recvBuffer = m_recvBuffer.mid(headerPos);
        }

        if (m_recvBuffer.size() < CommProtocol::MIN_FRAME_SIZE) {
            return;
        }

        const uint16_t payloadLen =
            (static_cast<uint8_t>(m_recvBuffer[2]) << 8) | static_cast<uint8_t>(m_recvBuffer[3]);
        const int totalFrameSize = CommProtocol::HEADER_SIZE + CommProtocol::LENGTH_SIZE +
                                   payloadLen + CommProtocol::CHECKSUM_SIZE;
        if (m_recvBuffer.size() < totalFrameSize) {
            return;
        }

        const QByteArray frame = m_recvBuffer.left(totalFrameSize);
        m_recvBuffer = m_recvBuffer.mid(totalFrameSize);

        const QByteArray checkData = frame.mid(CommProtocol::HEADER_SIZE,
                                               totalFrameSize - CommProtocol::HEADER_SIZE - 1);
        const uint8_t receivedCs = static_cast<uint8_t>(frame[totalFrameSize - 1]);
        const uint8_t calculatedCs = calcXorChecksum(checkData);
        if (receivedCs != calculatedCs) {
            qDebug() << "[CommManager] Frame checksum mismatch, frame dropped";
            continue;
        }

        emit commandReceived(static_cast<uint8_t>(frame[4]));
    }
}

void CommManager::sendMeasurement(double r0,
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
                                  uint32_t frameCount)
{
    if (!isConnected()) {
        return;
    }

    QByteArray data;
    data.resize(CommProtocol::MEASUREMENT_PAYLOAD_SIZE);
    int offset = 0;

    const uint64_t timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
    std::memcpy(data.data() + offset, &timestamp, 8);
    offset += 8;

    const float floatValues[] = {
        static_cast<float>(r0),
        static_cast<float>(seeing),
        static_cast<float>(theta0),
        static_cast<float>(tau0),
        static_cast<float>(centroid1X),
        static_cast<float>(centroid1Y),
        static_cast<float>(centroid2X),
        static_cast<float>(centroid2Y),
        static_cast<float>(peak1),
        static_cast<float>(peak2),
    };
    for (float value : floatValues) {
        std::memcpy(data.data() + offset, &value, 4);
        offset += 4;
    }

    const int32_t roiValues[] = {
        static_cast<int32_t>(roi1X),
        static_cast<int32_t>(roi1Y),
        static_cast<int32_t>(roi1W),
        static_cast<int32_t>(roi1H),
        static_cast<int32_t>(roi2X),
        static_cast<int32_t>(roi2Y),
        static_cast<int32_t>(roi2W),
        static_cast<int32_t>(roi2H),
    };
    for (int32_t value : roiValues) {
        std::memcpy(data.data() + offset, &value, 4);
        offset += 4;
    }

    std::memcpy(data.data() + offset, &frameCount, 4);

    m_socket->write(buildFrame(CommProtocol::CMD_MEASUREMENT, data));
}

void CommManager::sendDeviceStatus(float temp, float fps, bool cam0Connected, bool cam1Connected,
                                   bool isCapturing, uint32_t uptimeMs)
{
    if (!isConnected()) {
        return;
    }

    QByteArray data;
    data.resize(CommProtocol::DEVICE_STATUS_PAYLOAD_SIZE);
    int offset = 0;

    std::memcpy(data.data() + offset, &temp, 4);
    offset += 4;
    std::memcpy(data.data() + offset, &fps, 4);
    offset += 4;

    data[offset++] = static_cast<char>(cam0Connected ? 1 : 0);
    data[offset++] = static_cast<char>(cam1Connected ? 1 : 0);
    data[offset++] = static_cast<char>(isCapturing ? 1 : 0);

    std::memcpy(data.data() + offset, &uptimeMs, 4);
    m_socket->write(buildFrame(CommProtocol::CMD_DEVICE_STATUS, data));
}

void CommManager::sendAck(uint8_t ackCmd, uint8_t status)
{
    if (!isConnected()) {
        return;
    }

    QByteArray data;
    data.append(static_cast<char>(ackCmd));
    data.append(static_cast<char>(status));
    m_socket->write(buildFrame(CommProtocol::CMD_ACK, data));
}
