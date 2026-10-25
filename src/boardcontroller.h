#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include "session.h"
#include "terminalcontroller.h"

class BoardController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString output READ output NOTIFY changed)
    Q_PROPERTY(bool success READ success NOTIFY changed)
    Q_PROPERTY(TerminalController* terminal READ terminal NOTIFY terminalsChanged)
    Q_PROPERTY(QVariantList terminalSessions READ terminalSessions NOTIFY terminalsChanged)
    Q_PROPERTY(int terminalIndex READ terminalIndex NOTIFY terminalsChanged)
public:
    BoardController(Session *serial, Session *network, QObject *parent = nullptr);
    ~BoardController() override;
    bool busy() const { return m_busy; }
    QString output() const { return m_output; }
    bool success() const { return m_success; }
    TerminalController *terminal() { return m_terminals.at(m_terminalIndex); }
    QVariantList terminalSessions() const;
    int terminalIndex() const { return m_terminalIndex; }
    void setCatalog(CommandCatalog *catalog);
    Q_INVOKABLE void newTerminal();
    Q_INVOKABLE void selectTerminal(int index);
    Q_INVOKABLE void closeTerminal(int index);
    Q_INVOKABLE bool execute(int source, const QVariantMap &ssh, const QVariantMap &request);
    Q_INVOKABLE bool openTerminal(const QVariantMap &ssh);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void terminalsChanged();
private:
    bool sshArguments(const QVariantMap &ssh, QStringList *arguments, bool batch);
    void frame(Session *session, const QByteArray &bytes);
    void finish(bool success, const QString &output);
    void reply(const QJsonObject &object);
    Session *m_serial, *m_network, *m_selected = nullptr;
    QProcess m_process;
    QList<TerminalController*> m_terminals;
    QPointer<CommandCatalog> m_catalog;
    int m_terminalIndex = 0;
    QTimer m_timer;
    QByteArray m_stdout, m_stderr, m_head, m_tail;
    QString m_output = QStringLiteral("选择数据来源，配置板端参数后执行。"), m_bus, m_abortReason;
    int m_id = 0, m_nextId = 1, m_mode = 1;
    bool m_busy = false, m_success = true;
};
