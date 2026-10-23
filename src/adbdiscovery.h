#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariant>

class AdbDiscovery : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString executablePath READ executablePath WRITE setExecutablePath NOTIFY changed)
    Q_PROPERTY(QString resolvedPath READ resolvedPath NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QString selectedSerial READ selectedSerial WRITE setSelectedSerial NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool persistenceEnabled READ persistenceEnabled WRITE setPersistenceEnabled NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit AdbDiscovery(QObject *parent=nullptr);
    ~AdbDiscovery() override;
    QString executablePath() const {return m_path;}
    QString resolvedPath() const {return m_resolved;}
    QVariantList devices() const {return m_devices;}
    QString selectedSerial() const {return m_selected;}
    bool enabled() const {return m_enabled;}
    bool persistenceEnabled() const {return m_persistenceEnabled;}
    bool scanning() const {return m_scanning;}
    QString status() const {return m_status;}
    bool ready() const;
    void setExecutablePath(const QString &path);
    void setSelectedSerial(const QString &serial);
    void setEnabled(bool enabled);
    void setPersistenceEnabled(bool enabled);
    Q_INVOKABLE void refresh();
    static QString resolveExecutable(const QString &path);
signals:
    void changed();
private:
    void collect();
    void complete(int code,QProcess::ExitStatus exit);
    QProcess m_process;
    QTimer m_poll,m_timeout;
    QByteArray m_output,m_errors;
    QVariantList m_devices;
    QString m_path,m_resolved,m_selected,m_pendingSerial,m_failure;
    QString m_status=QStringLiteral("切换到 Android 后自动检测 USB / ADB 设备");
    bool m_enabled=false,m_scanning=false,m_restart=false,m_persistenceEnabled=true;
};
