#pragma once
#include "adbdiscovery.h"
#include <QHostAddress>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QVariant>

class QTcpSocket;
class QUdpSocket;

// 只读探索；主动 TCP 探测仅由 scanSubnet 显式启动。
class DeviceDiscovery : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QVariantList interfaces READ interfaces NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(AdbDiscovery* adb READ adb CONSTANT)
public:
    explicit DeviceDiscovery(QObject *parent=nullptr, bool monitor=true, quint16 listenPort=45454);
    ~DeviceDiscovery() override;
    QVariantList devices() const { return m_devices; }
    QVariantList interfaces() const { return m_interfaces; }
    bool scanning() const { return m_refreshing || m_scanning; }
    QString status() const { return m_status; }
    quint16 listenPort() const;
    AdbDiscovery *adb() { return &m_adb; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setSavedDevices(const QVariantList &devices);
    Q_INVOKABLE bool scanSubnet(const QString &cidr, const QVariantList &ports={});
    Q_INVOKABLE void cancelScan();
    static QVariantList normalizedDevices(const QVariantList &devices);
    static QStringList subnetTargets(const QString &cidr, int portCount, QString *error=nullptr);
    static QVariantMap parseAnnouncement(const QByteArray &data, const QHostAddress &sender);
signals:
    void changed();
private:
    void rebuild();
    void pumpScan();
    void finishProbe(QTcpSocket *socket, bool available);
    void receiveAnnouncements();
    AdbDiscovery m_adb;
    QTimer m_poll;
    QUdpSocket *m_broadcast=nullptr;
    quint16 m_requestedPort=45454;
    QVariantList m_devices,m_interfaces,m_local,m_saved,m_probed;
    QMap<QString,QPair<qint64,QVariantMap>> m_announced;
    QString m_status=QStringLiteral("尚未刷新设备");
    struct Probe { QString host; quint16 port=0; };
    QList<Probe> m_tasks;
    QSet<QTcpSocket*> m_sockets;
    int m_next=0,m_completed=0;
    bool m_refreshing=false,m_scanning=false,m_monitor=true;
};
