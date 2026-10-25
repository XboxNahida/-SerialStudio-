#pragma once
#include "commandcatalog.h"
#include <QNetworkAccessManager>
#include <functional>

class CatalogUpdater final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
    Q_PROPERTY(bool canDownload READ canDownload NOTIFY changed)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString lastChecked READ lastChecked NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
public:
    CatalogUpdater(CommandCatalog *catalog,QString appVersion,QNetworkAccessManager *network=nullptr,QObject *parent=nullptr);
    bool busy() const {return m_busy;}
    bool updateAvailable() const {return m_available;}
    bool canDownload() const {return m_available&&m_compatible&&!m_downloadUrl.isEmpty();}
    QString latestVersion() const {return m_latest;}
    QString status() const {return m_status;}
    QString lastChecked() const {return m_checked;}
    double progress() const {return m_progress;}
    Q_INVOKABLE void check(bool manual=true);
    Q_INVOKABLE void download();
    Q_INVOKABLE void ignore();
signals:
    void changed();
private:
    void fetch(const QUrl &url,qint64 limit,std::function<void(QByteArray)> done);
    void fail(const QString &message);
    CommandCatalog *m_catalog;
    QNetworkAccessManager *m_network;
    QString m_appVersion,m_latest,m_downloadUrl,m_hash,m_minAppVersion,m_checked;
    QString m_status=QStringLiteral("尚未检查指令库更新");
    qint64 m_size=0;
    bool m_busy=false,m_available=false,m_compatible=false;
    double m_progress=0;
};
