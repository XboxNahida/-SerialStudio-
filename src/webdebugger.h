#pragma once
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QVariant>

class QNetworkReply;
class SystemProxy;
class WebDebugger : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int statusCode READ statusCode NOTIFY changed)
    Q_PROPERTY(QVariantList responseHeaders READ responseHeaders NOTIFY changed)
    Q_PROPERTY(QString responseBody READ responseBody NOTIFY changed)
    Q_PROPERTY(qint64 elapsed READ elapsed NOTIFY changed)
public:
    explicit WebDebugger(SystemProxy *proxy=nullptr,QObject *parent=nullptr);
    bool busy() const { return !m_reply.isNull(); }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    int statusCode() const { return m_code; }
    QVariantList responseHeaders() const { return m_headers; }
    QString responseBody() const { return QString::fromUtf8(m_body); }
    qint64 elapsed() const { return busy()?m_clock.elapsed():m_elapsed; }
    Q_INVOKABLE bool request(const QVariantMap &config);
    Q_INVOKABLE void cancel();
signals:
    void changed();
private:
    void collect();
    void finish();
    QNetworkAccessManager m_manager;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    QElapsedTimer m_clock;
    qint64 m_elapsed=0;
    QByteArray m_body;
    QVariantList m_headers;
    QString m_status=QStringLiteral("尚未发送 HTTP 请求"),m_error;
    int m_code=0;
    bool m_cancelled=false;
    SystemProxy *m_proxy=nullptr;
};
