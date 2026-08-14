#include "CommManager.h"

#include "CommProtocol.h"

#include <QNetworkProxy>

namespace {

QString socketErrorText(QAbstractSocket::SocketError error, const QString& fallback)
{
    switch (error) {
    case QAbstractSocket::ConnectionRefusedError:
        return QStringLiteral("连接被拒绝，请检查目标主机和端口是否已开启。");
    case QAbstractSocket::RemoteHostClosedError:
        return QStringLiteral("远端主机已断开连接。");
    case QAbstractSocket::HostNotFoundError:
        return QStringLiteral("未找到目标主机，请检查 IP 地址。");
    case QAbstractSocket::SocketAccessError:
        return QStringLiteral("套接字访问被拒绝，请检查系统权限或防火墙设置。");
    case QAbstractSocket::SocketTimeoutError:
        return QStringLiteral("网络通信超时。");
    case QAbstractSocket::NetworkError:
        return QStringLiteral("网络异常，请检查网线或网络配置。");
    default:
        return fallback.isEmpty() ? QStringLiteral("未知通信错误。") : fallback;
    }
}

}  // namespace

CommManager::CommManager(QObject* parent)
    : QObject(parent)
{
    m_socket = new QTcpSocket(this);
    m_socket->setProxy(QNetworkProxy::NoProxy);
    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);

    connect(m_socket, &QTcpSocket::connected, this, &CommManager::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &CommManager::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &CommManager::onError);
    connect(m_connectTimer, &QTimer::timeout, this, &CommManager::onConnectTimeout);
}

void CommManager::connectToHost(const QString& ip, quint16 port)
{
    QHostAddress address;
    if (!address.setAddress(ip) || port == 0) {
        emit connectionError(QStringLiteral("无效的网络地址或端口。"));
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
    m_sequence = 0;
    emit connected();
}

void CommManager::onDisconnected()
{
    if (m_connectTimer->isActive()) {
        m_connectTimer->stop();
    }
    emit disconnected();
}

void CommManager::onError(QAbstractSocket::SocketError error)
{
    if (m_connectTimer->isActive()) {
        m_connectTimer->stop();
    }
    emit connectionError(socketErrorText(error, m_socket->errorString()));
}

void CommManager::onConnectTimeout()
{
    if (m_socket->state() == QAbstractSocket::ConnectingState) {
        m_socket->abort();
        emit connectionError(QStringLiteral("连接超时，请检查上位机 IP、端口和防火墙设置。"));
    }
}

void CommManager::sendMonitoringFrame(float temperatureC,
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
                                      std::uint64_t timestampMs)
{
    if (!isConnected()) {
        return;
    }

    const QByteArray data = CommProtocol::buildData(temperatureC,
                                                     humidityRh,
                                                     pressureHpa,
                                                     r0,
                                                     seeing,
                                                     theta0,
                                                     tau0,
                                                     peakBrightnessCameraA,
                                                     peakBrightnessCameraB,
                                                     exposureTimeCameraAUs,
                                                     exposureTimeCameraBUs,
                                                     frameRateHz,
                                                     deviceStatus);
    const QByteArray frame = CommProtocol::buildMonitoringFrame(m_sequence++, timestampMs, data);
    if (frame.size() == CommProtocol::FRAME_SIZE) {
        m_socket->write(frame);
    }
}
