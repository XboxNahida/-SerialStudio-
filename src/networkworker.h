#pragma once
#include <QObject>
#include <QVariantMap>
class QTcpSocket;
class QTcpServer;
class QUdpSocket;
class QTimer;

class NetworkWorker : public QObject {
    Q_OBJECT
public:
    quint16 localPort() const;
public slots:
    void open(const QVariantMap &config);
    void close();
    void write(const QByteArray &bytes);
    void acknowledgeReceive();
signals:
    void opened(const QString &description, bool writable);
    void availability(bool writable, const QString &description);
    void closed();
    void received(const QByteArray &bytes, bool fromTarget);
    void transmitted(qint64 count);
    void fault(const QString &message);
private:
    void attach(QTcpSocket *socket);
    void drain();
    QTcpSocket *m_tcp = nullptr;
    QTcpServer *m_server = nullptr;
    QUdpSocket *m_udp = nullptr;
    QTimer *m_timeout = nullptr;
    QString m_host;
    quint16 m_targetPort = 0;
    int m_mode = 0, m_inflight = 0;
    bool m_active = false;
};
