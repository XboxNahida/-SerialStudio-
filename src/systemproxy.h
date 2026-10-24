#pragma once
#include <QObject>
#include <QNetworkProxy>
#include <QUrl>
#include <QNetworkProxyQuery>
#include <functional>

class QNetworkAccessManager;

class SystemProxy final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool configured READ configured NOTIFY changed)
public:
    using Query = std::function<QList<QNetworkProxy>(const QNetworkProxyQuery &)>;
    explicit SystemProxy(QObject *parent=nullptr);
    explicit SystemProxy(Query query, QObject *parent=nullptr);
    QString source() const { return m_source; }
    QString status() const { return m_status; }
    bool configured() const { return m_configured; }
    QNetworkProxy proxyFor(const QUrl &url) const;
    QNetworkProxy socketProxy(const QString &host, quint16 port) const;
    QString describe(const QUrl &url) const;
    Q_INVOKABLE void refresh();
    void apply(QNetworkAccessManager *manager) const;
signals:
    void changed();
private:
    void updateSnapshot(const QNetworkProxy &http, const QNetworkProxy &https);
    QString m_source=QStringLiteral("自动跟随系统代理");
    QString m_status=QStringLiteral("正在读取示例目标的系统代理状态…");
    bool m_configured=false;
    bool m_refreshing=false;
    Query m_query;
};
