#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariant>
#include "adbdiscovery.h"

class TerminalController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool inputBusy READ inputBusy NOTIFY changed)
    Q_PROPERTY(int mode READ mode NOTIFY changed)
    Q_PROPERTY(qint64 processId READ processId NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(AdbDiscovery* adb READ adb CONSTANT)
public:
    explicit TerminalController(QObject *parent=nullptr);
    ~TerminalController() override;
    bool active() const;
    bool inputBusy() const {return m_input.state()!=QProcess::NotRunning;}
    int mode() const {return m_mode;}
    qint64 processId() const {return m_pid;}
    QString status() const {return m_status;}
    AdbDiscovery *adb() {return &m_adb;}
    Q_INVOKABLE bool open(int mode,const QVariantMap &config);
    Q_INVOKABLE bool fill(const QString &text);
    Q_INVOKABLE bool sendKey(const QString &key);
    Q_INVOKABLE QVariantList commands(int mode,const QString &search=QString(),const QString &category=QString()) const;
    static bool launchSpec(int mode,const QVariantMap &config,QString *program,QStringList *arguments,QString *error);
    static bool validText(const QString &text);
    static int inputHelper(const QByteArray &request);
signals:
    void changed();
private:
    bool fail(const QString &message);
    bool dispatch(const QString &kind,const QString &value);
    void refresh();
    QProcess m_input;
    AdbDiscovery m_adb;
    QTimer m_poll,m_timeout;
    QVariantList m_commands;
    void *m_handle=nullptr;
    qint64 m_pid=0;
    quint64 m_started=0;
    int m_mode=-1;
    QString m_status=QStringLiteral("尚未打开交互终端");
};
