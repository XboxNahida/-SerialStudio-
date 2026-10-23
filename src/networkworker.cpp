#include "networkworker.h"
#include <QTcpSocket>
#include <QTcpServer>
#include <QUdpSocket>
#include <QTimer>

quint16 NetworkWorker::localPort() const
{ return m_udp ? m_udp->localPort() : m_server ? m_server->serverPort() : m_tcp ? m_tcp->localPort() : 0; }
void NetworkWorker::open(const QVariantMap &c)
{
    close();
    bool validMode = false, validPort = false, validLocal = false;
    m_mode = c.value("mode").toInt(&validMode);
    const auto port = c.value("port").toInt(&validPort);
    const auto local = c.value("localPort", 0).toInt(&validLocal);
    m_host = c.value("host").toString().trimmed();
    QHostAddress address;
    if (!validMode || m_mode < 0 || m_mode > 2 || !validPort || port < (m_mode == 1 ? 0 : 1)
        || port > 65535 || !validLocal || local < 0 || local > 65535 || m_host.isEmpty()
        || (m_mode != 0 && !address.setAddress(m_host))) {
        emit fault(QStringLiteral("网络参数无效：检查模式、地址和端口；服务端/UDP 请使用 IP 地址"));
        emit closed(); return;
    }
    m_targetPort = quint16(port); m_active = true;
    if (m_mode == 0) {
        attach(new QTcpSocket(this));
        m_timeout = new QTimer(this); m_timeout->setSingleShot(true);
        connect(m_timeout, &QTimer::timeout, this, [this] { emit fault(QStringLiteral("TCP 连接超时")); close(); });
        connect(m_tcp, &QTcpSocket::connected, this, [this] {
            if (m_timeout) m_timeout->stop();
            emit opened(QStringLiteral("TCP 已连接 · %1:%2").arg(m_host).arg(m_targetPort), true);
        });
        m_timeout->start(10000); m_tcp->connectToHost(m_host, m_targetPort);
    } else if (m_mode == 1) {
        m_server = new QTcpServer(this); m_server->setMaxPendingConnections(1);
        connect(m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server && m_server->hasPendingConnections()) {
                auto peer = m_server->nextPendingConnection();
                if (m_tcp) { peer->abort(); peer->deleteLater(); continue; }
                attach(peer);
                emit availability(true, QStringLiteral("TCP 对端 · %1:%2").arg(peer->peerAddress().toString()).arg(peer->peerPort()));
                drain();
            }
        });
        if (!m_server->listen(address, m_targetPort)) { emit fault(m_server->errorString()); close(); return; }
        emit opened(QStringLiteral("TCP 监听 · %1:%2 · 等待对端").arg(m_host).arg(localPort()), false);
    } else {
        QHostAddress bind;
        if (!bind.setAddress(c.value("bind", "0.0.0.0").toString())) {
            emit fault(QStringLiteral("UDP 本地绑定地址无效")); close(); return;
        }
        m_udp = new QUdpSocket(this); m_udp->setReadBufferSize(262144);
        connect(m_udp, &QUdpSocket::readyRead, this, &NetworkWorker::drain);
        if (!m_udp->bind(bind, quint16(local))) { emit fault(m_udp->errorString()); close(); return; }
        emit opened(QStringLiteral("UDP 本地 %1:%2 → %3:%4").arg(bind.toString()).arg(localPort()).arg(m_host).arg(m_targetPort), true);
    }
}
void NetworkWorker::attach(QTcpSocket *socket)
{
    m_tcp = socket; socket->setParent(this); socket->setReadBufferSize(262144);
    connect(socket, &QTcpSocket::readyRead, this, &NetworkWorker::drain);
    connect(socket, &QTcpSocket::bytesWritten, this, &NetworkWorker::transmitted);
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError error) {
        if (m_tcp != socket || error == QAbstractSocket::RemoteHostClosedError) return;
        emit fault(socket->errorString());
        if (!m_server) close();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
        if (m_tcp != socket) return;
        drain();
        if (socket->bytesAvailable()) emit fault(QStringLiteral("TCP 对端断开时仍有未处理数据，本次采集不完整"));
        if (!m_server) { close(); return; }
        socket->disconnect(this); socket->deleteLater(); m_tcp = nullptr;
        emit availability(false, QStringLiteral("TCP 监听中 · 对端已断开，等待新连接"));
    });
}
void NetworkWorker::close()
{
    const bool wasActive = m_active; m_active = false;
    if (m_timeout) { m_timeout->stop(); m_timeout->deleteLater(); m_timeout = nullptr; }
    if (m_server) { m_server->disconnect(this); m_server->close(); m_server->deleteLater(); m_server = nullptr; }
    if (m_tcp) { m_tcp->disconnect(this); m_tcp->abort(); m_tcp->deleteLater(); m_tcp = nullptr; }
    if (m_udp) { m_udp->disconnect(this); m_udp->close(); m_udp->deleteLater(); m_udp = nullptr; }
    // 旧连接的接收确认可能还在队列中，保留 inflight 直到确认回来。
    if (wasActive) emit closed();
}
void NetworkWorker::write(const QByteArray &bytes)
{
    if (m_udp) {
        if (bytes.size() > 65507) { emit fault(QStringLiteral("UDP 单包最多 65507 字节")); return; }
        const auto count = m_udp->writeDatagram(bytes, QHostAddress(m_host), m_targetPort);
        if (count != bytes.size()) emit fault(m_udp->errorString()); else emit transmitted(count);
    } else if (m_tcp && m_tcp->state() == QAbstractSocket::ConnectedState) {
        if (m_tcp->bytesToWrite() > 262144) { emit fault(QStringLiteral("TCP 发送队列繁忙，请降低发送频率")); return; }
        if (m_tcp->write(bytes) != bytes.size()) emit fault(QStringLiteral("TCP 数据未完整加入发送队列"));
    } else emit fault(QStringLiteral("网络尚无可发送的对端"));
}
void NetworkWorker::drain()
{
    while (m_inflight < 8) {
        QByteArray bytes;
        bool fromTarget = true;
        if (m_udp && m_udp->hasPendingDatagrams()) {
            bytes.resize(m_udp->pendingDatagramSize());
            QHostAddress sender; quint16 senderPort = 0;
            const auto count = m_udp->readDatagram(bytes.data(), bytes.size(), &sender, &senderPort);
            if (count < 0) { emit fault(m_udp->errorString()); break; }
            fromTarget = sender.isEqual(QHostAddress(m_host), QHostAddress::TolerantConversion) && senderPort == m_targetPort;
        } else if (m_tcp && m_tcp->bytesAvailable()) bytes = m_tcp->read(32768);
        else break;
        ++m_inflight; emit received(bytes, fromTarget);
    }
    if (m_inflight >= 8 && ((m_tcp && m_tcp->bytesAvailable() >= 262144) || (m_udp && m_udp->hasPendingDatagrams()))) {
        emit fault(QStringLiteral("网络接收积压超限，已关闭；本次采集可能不完整")); close();
    }
}
void NetworkWorker::acknowledgeReceive() { if (m_inflight > 0) --m_inflight; drain(); }
