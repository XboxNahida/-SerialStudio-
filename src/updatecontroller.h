#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <functional>
class UpdateController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
    Q_PROPERTY(bool canDownload READ canDownload NOTIFY changed)
    Q_PROPERTY(bool downloadReady READ downloadReady NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool uninstallAvailable READ uninstallAvailable CONSTANT)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
public:
    explicit UpdateController(QString currentVersion,QNetworkAccessManager *network=nullptr,QObject *parent=nullptr);
    static int compareVersions(const QString &left,const QString &right);
    bool busy() const {return m_busy;}
    QString latestVersion() const {return m_latest;}
    bool updateAvailable() const {return m_available;}
    bool canDownload() const {return m_available&&!m_assetUrl.isEmpty()&&!m_hashUrl.isEmpty();}
    bool downloadReady() const {return m_ready;}
    QString status() const {return m_status;}
    bool uninstallAvailable() const;
    double progress() const {return m_progress;}
    Q_INVOKABLE void check(bool manual=true);
    Q_INVOKABLE void ignore();
    Q_INVOKABLE void download();
    Q_INVOKABLE void install();
    Q_INVOKABLE void uninstall();
    Q_INVOKABLE void openRelease();
signals:
    void changed();
    void exitRequested();
private:
    friend class UpdateTests;
    void fetch(const QUrl &url,qint64 limit,std::function<void(QByteArray)> done);
    void fail(const QString &message);
    bool verifyFile() const;
    void pruneCache();
    QNetworkAccessManager *m_network;
    QString m_current,m_latest,m_tag,m_releaseUrl,m_assetUrl,m_hashUrl,m_assetName;
    QString m_cacheRoot,m_downloadPath,m_expectedHash,m_status=QStringLiteral("尚未检查更新");
    qint64 m_assetSize=0;
    bool m_busy=false,m_available=false,m_ready=false;
    double m_progress=0;
    std::function<bool(const QString &,const QStringList &)> m_launch;
};
