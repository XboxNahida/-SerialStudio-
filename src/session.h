#pragma once
#include "dataworkspace.h"
#include "eventmodel.h"
#include "protocol.h"
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

class SerialWorker;
class NetworkWorker;
class Session : public QObject {
    Q_OBJECT
    Q_PROPERTY(DataWorkspace* analysis READ analysis CONSTANT)
    Q_PROPERTY(QVariantList ports READ ports NOTIFY portsChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool writable READ writable NOTIFY stateChanged)
    Q_PROPERTY(int transport READ transport NOTIFY stateChanged)
    Q_PROPERTY(QString portName READ portName NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(qulonglong rxBytes READ rxBytes NOTIFY statsChanged)
    Q_PROPERTY(qulonglong txBytes READ txBytes NOTIFY statsChanged)
    Q_PROPERTY(double rxRate READ rxRate NOTIFY statsChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY statsChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY statsChanged)
    Q_PROPERTY(bool periodic READ periodic NOTIFY stateChanged)
    Q_PROPERTY(int parserMode READ parserMode NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap preferences READ preferences CONSTANT)
    Q_PROPERTY(EventModel* terminal READ terminal CONSTANT)
    Q_PROPERTY(EventModel* frames READ frames CONSTANT)
public:
    DataWorkspace *analysis() {return &m_analysis;}
    explicit Session(QObject *parent = nullptr, bool persist = true);
    ~Session() override;
    QVariantList ports() const { return m_ports; }
    bool connected() const { return m_connected; }
    bool busy() const { return m_busy; }
    bool writable() const { return m_connected && !m_busy && m_canWrite; }
    int transport() const { return m_transport; }
    QString portName() const { return m_portName; }
    QString status() const { return m_status; }
    QString lastError() const { return m_lastError; }
    qulonglong rxBytes() const { return m_rx; }
    qulonglong txBytes() const { return m_tx; }
    double rxRate() const { return m_rate; }
    int frameCount() const { return m_frameCount; }
    int errorCount() const { return m_errorCount; }
    bool periodic() const { return m_repeat.isActive(); }
    int parserMode() const { return int(m_mode); }
    QVariantMap preferences() const;
    EventModel *terminal() { return &m_terminal; }
    EventModel *frames() { return &m_frames; }
    Q_INVOKABLE void refreshPorts();
    Q_INVOKABLE bool openPort(const QVariantMap &config);
    Q_INVOKABLE bool openNetwork(const QVariantMap &config);
    Q_INVOKABLE void closePort();
    Q_INVOKABLE bool send(const QString &text, bool hex, int newline);
    Q_INVOKABLE bool startPeriodic(const QString &text, bool hex, int newline, int interval);
    Q_INVOKABLE void stopPeriodic();
    Q_INVOKABLE bool configureParser(int mode, const QString &head, const QString &tail);
    Q_INVOKABLE void clearTerminal();
    Q_INVOKABLE void clearFrames();
    Q_INVOKABLE void resetStats();
    Q_INVOKABLE void demo();
    Q_INVOKABLE bool saveLog(const QUrl &url);
    Q_INVOKABLE void savePreference(const QString &key, const QVariant &value);
    Q_INVOKABLE void setLines(bool dtr, bool rts);
    bool sendBytes(const QByteArray &bytes, const QString &title);
    QByteArray bridgeHead() const { return m_customHead; }
    QByteArray bridgeTail() const { return m_customTail; }
signals:
    void portsChanged();
    void stateChanged();
    void statsChanged();
    void notify(const QString &message);
    void requestOpen(const QVariantMap &config);
    void requestClose();
    void requestWrite(const QByteArray &bytes);
    void requestLines(bool dtr, bool rts);
    void receivedConsumed();
    void requestNetworkOpen(const QVariantMap &config);
    void requestNetworkClose();
    void requestNetworkWrite(const QByteArray &bytes);
    void networkConsumed();
    void frameReceived(const QByteArray &bytes);
private:
    DataWorkspace m_analysis;
    void disconnected();
    void writeBytes(const QByteArray &bytes);
    void report(const QString &message);
    void receive(const QByteArray &bytes, bool demoSource = false, bool bridgeEligible = true);
    void collect(const QList<ParsedFrame> &frames, bool demoSource);
    void flushUi();
    bool makePayload(const QString &text, bool hex, int newline, QByteArray *bytes);
    void enqueue(QList<EventEntry> &queue, EventEntry event);
    QThread m_thread;
    SerialWorker *m_worker;
    NetworkWorker *m_network;
    EventModel m_terminal{1000, this}, m_frames{500, this};
    QList<EventEntry> m_pendingTerminal, m_pendingFrames;
    QTimer m_uiTimer, m_idle, m_repeat, m_rateTimer;
    QSettings m_settings;
    QVariantList m_ports;
    QVariantMap m_config;
    ProtocolParser m_parser;
    ProtocolParser::Mode m_mode = ProtocolParser::Lines;
    QByteArray m_repeatBytes;
    QByteArray m_customHead = QByteArray::fromHex("aa55"), m_customTail = QByteArray::fromHex("0d0a");
    bool m_connected = false, m_busy = false, m_persist = true;
    bool m_canWrite = true;
    int m_transport = 0;
    bool m_demoPending = false;
    bool m_bridgeEligible = true;
    QString m_portName, m_status = QStringLiteral("就绪 · 未连接"), m_lastError;
    qulonglong m_rx = 0, m_tx = 0, m_previousRx = 0, m_pendingDropped = 0;
    double m_rate = 0;
    int m_frameCount = 0, m_errorCount = 0;
};
