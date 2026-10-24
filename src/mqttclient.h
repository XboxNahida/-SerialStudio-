#pragma once
#include <QElapsedTimer>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariant>

class QAbstractSocket;
class SystemProxy;
class MqttClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY changed)
    Q_PROPERTY(QStringList subscriptions READ subscriptions NOTIFY changed)
public:
    explicit MqttClient(SystemProxy *proxy=nullptr,QObject *parent=nullptr);
    ~MqttClient() override;
    bool connected() const { return m_connected; }
    bool busy() const { return m_connecting || !m_pending.isEmpty(); }
    QString status() const { return m_status; }
    QVariantList messages() const { return m_messages; }
    int count() const { return int(m_messages.size()); }
    int selectedCount() const { return int(m_selected.size()); }
    QStringList subscriptions() const { return m_subscriptions; }
    Q_INVOKABLE bool connectBroker(const QVariantMap &config);
    Q_INVOKABLE void disconnectBroker();
    Q_INVOKABLE bool publish(const QString &topic,const QString &text,int qos=0,bool retain=false);
    Q_INVOKABLE bool subscribe(const QString &topic,int qos=0);
    Q_INVOKABLE bool unsubscribe(const QString &topic);
    Q_INVOKABLE void clear();
    Q_INVOKABLE bool setEventSelected(const QString &id,bool selected);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QString selectedText(bool hex,bool detailed,bool protocol=false) const;
signals:
    void changed();
    void modelReset();
    void messageReceived(const QString &topic,const QByteArray &payload);
private:
    void sendConnect();
    bool sendPacket(quint8 header,const QByteArray &body);
    void receive();
    bool processPacket(quint8 header,const QByteArray &body);
    void close(const QString &status);
    void tick();
    quint16 nextId();
    struct Pending { int type=0; QString topic; qint64 sent=0; int qos=0; };
    QMap<quint16,Pending> m_pending;
    QPointer<QAbstractSocket> m_socket;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QVariantMap m_config;
    QByteArray m_input;
    QVariantList m_messages;
    QSet<QString> m_selected;
    qulonglong m_eventId=0;
    QStringList m_subscriptions;
    QString m_status=QStringLiteral("MQTT 未连接");
    bool m_connected=false,m_connecting=false,m_waitPing=false;
    quint16 m_packetId=0;
    int m_keepAlive=60;
    qint64 m_lastSent=0,m_pingSent=0,m_connectStarted=0;
    qsizetype m_messageBytes=0;
    SystemProxy *m_proxy=nullptr;
};
