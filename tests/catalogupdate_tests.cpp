#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSettings>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include "catalogupdater.h"

class CatalogReply final : public QNetworkReply {
    QByteArray m_bytes; qint64 m_offset=0;
public:
    CatalogReply(const QNetworkRequest &r,QByteArray bytes,QObject *p):QNetworkReply(p),m_bytes(std::move(bytes)) {
        setRequest(r);setUrl(r.url());setAttribute(QNetworkRequest::HttpStatusCodeAttribute,200);open(ReadOnly);
        QTimer::singleShot(0,this,[this]{emit readyRead();setFinished(true);emit finished();});
    }
    void abort() override {setError(OperationCanceledError,"cancelled");}
    qint64 bytesAvailable() const override {return m_bytes.size()-m_offset+QNetworkReply::bytesAvailable();}
protected:
    qint64 readData(char *data,qint64 max) override {const auto n=qMin(max,qint64(m_bytes.size())-m_offset);if(n<=0)return -1;memcpy(data,m_bytes.constData()+m_offset,size_t(n));m_offset+=n;return n;}
};
class CatalogNetwork final : public QNetworkAccessManager {
public:
    QList<QByteArray> responses;QStringList urls;
protected:
    QNetworkReply *createRequest(Operation,const QNetworkRequest &r,QIODevice *) override {
        urls<<r.url().toString();return new CatalogReply(r,responses.isEmpty()?QByteArray():responses.takeFirst(),this);
    }
};
class CatalogUpdateTests : public QObject {
    Q_OBJECT
    QString base="https://github.com/XboxNahida/-SerialStudio-/releases/download/catalog-v1.1.0/";
    QByteArray library() {
        QFile f(QCoreApplication::applicationDirPath()+"/catalog/terminal_commands.json");if(!f.open(QIODevice::ReadOnly))return {};
        auto object=QJsonDocument::fromJson(f.readAll()).object();object["catalogVersion"]="1.1.0";return QJsonDocument(object).toJson();
    }
    QByteArray feed(bool evil=false) {
        return QJsonDocument(QJsonArray{QJsonObject{{"tag_name","catalog-v1.1.0"},{"draft",false},{"prerelease",false},
            {"html_url","https://github.com/XboxNahida/-SerialStudio-/releases/tag/catalog-v1.1.0"},
            {"assets",QJsonArray{QJsonObject{{"name","terminal-catalog-manifest.json"},{"browser_download_url",evil?"https://example.com/file.json":base+"terminal-catalog-manifest.json"}}}}}}).toJson();
    }
    QByteArray manifest(const QByteArray &data,bool badHash=false,const QString &minimum="1.3.0") {
        const auto name="terminal-catalog-v1.1.0.json";
        return QJsonDocument(QJsonObject{{"schemaVersion",1},{"catalogVersion","1.1.0"},{"minAppVersion",minimum},
            {"fileName",name},{"size",data.size()},{"sha256",badHash?QString(64,'0'):QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex())},
            {"downloadUrl",base+name},{"publishedAt","2026-10-25T00:00:00Z"}}).toJson();
    }
private slots:
    void independentIgnoreAndApply() {
        QSettings().clear();QSettings().setValue("updates/ignoredTag","v99.0.0");
        QTemporaryDir dir;CommandCatalog store("1.3.0",dir.path());CatalogNetwork network;CatalogUpdater updater(&store,"1.3.0",&network);
        const auto data=library();QVERIFY(!data.isEmpty());network.responses<<feed()<<manifest(data);
        updater.check(false);QTRY_VERIFY(!updater.busy());QVERIFY(updater.updateAvailable());QVERIFY(updater.canDownload());
        updater.ignore();QVERIFY(!updater.updateAvailable());QCOMPARE(QSettings().value("updates/ignoredTag").toString(),QString("v99.0.0"));
        network.responses<<feed()<<manifest(data);updater.check(false);QTRY_VERIFY(!updater.busy());QVERIFY(!updater.updateAvailable());
        network.responses<<feed()<<manifest(data);updater.check(true);QTRY_VERIFY(!updater.busy());QVERIFY(updater.updateAvailable());
        network.responses<<data;updater.download();QTRY_VERIFY(!updater.busy());QCOMPARE(store.version(),QString("1.1.0"));QVERIFY(!updater.updateAvailable());
        QVERIFY(store.hasPrevious());QVERIFY(updater.lastChecked().size()>0);
    }
    void rejectTamperCompatibilityAndForeignSources() {
        QSettings().clear();QTemporaryDir dir;CommandCatalog store("1.3.0",dir.path());CatalogNetwork network;CatalogUpdater updater(&store,"1.3.0",&network);
        const auto data=library();network.responses<<feed()<<manifest(data,true)<<data;
        updater.check();QTRY_VERIFY(!updater.busy());updater.download();QTRY_VERIFY(!updater.busy());QCOMPARE(store.version(),QString("1.0.0"));QVERIFY(updater.status().contains(QStringLiteral("SHA256")));
        network.responses<<feed()<<manifest(data,false,"99.0.0");updater.check();QTRY_VERIFY(!updater.busy());QVERIFY(updater.updateAvailable());QVERIFY(!updater.canDownload());
        network.responses<<feed(true);updater.check();QTRY_VERIFY(!updater.busy());QVERIFY(!updater.canDownload());QCOMPARE(store.version(),QString("1.0.0"));
        network.responses<<"bad json";updater.check();QTRY_VERIFY(!updater.busy());QCOMPARE(store.version(),QString("1.0.0"));
    }
    void manifestAndLibraryVersionsMustMatch() {
        QSettings().clear();QTemporaryDir dir;CommandCatalog store("1.3.0",dir.path());CatalogNetwork network;CatalogUpdater updater(&store,"1.3.0",&network);
        QFile f(QCoreApplication::applicationDirPath()+"/catalog/terminal_commands.json");QVERIFY(f.open(QIODevice::ReadOnly));const auto old=f.readAll();
        network.responses<<feed()<<manifest(old)<<old;updater.check();QTRY_VERIFY(!updater.busy());updater.download();QTRY_VERIFY(!updater.busy());
        QCOMPARE(store.version(),QString("1.0.0"));QVERIFY(updater.status().contains(QStringLiteral("版本")));
    }
    void releaseChannelsAndSizeLimits() {
        QSettings().clear();QTemporaryDir dir;CommandCatalog store("1.3.0",dir.path());CatalogNetwork network;CatalogUpdater updater(&store,"1.3.0",&network);
        const auto data=library();auto releases=QJsonDocument::fromJson(feed()).array();
        auto draft=releases[0].toObject();draft["draft"]=true;draft["tag_name"]="catalog-v9.0.0";
        draft["html_url"]="https://github.com/XboxNahida/-SerialStudio-/releases/tag/catalog-v9.0.0";
        auto beta=draft;beta["draft"]=false;beta["prerelease"]=true;beta["tag_name"]="catalog-v9.0.0-beta.1";
        beta["html_url"]="https://github.com/XboxNahida/-SerialStudio-/releases/tag/catalog-v9.0.0-beta.1";
        releases.prepend(beta);releases.prepend(draft);
        network.responses<<QJsonDocument(releases).toJson()<<manifest(data);
        updater.check();QTRY_VERIFY(!updater.busy());QCOMPARE(updater.latestVersion(),QString("1.1.0"));
        auto oversized=QJsonDocument::fromJson(manifest(data)).object();oversized["size"]=CommandCatalog::maximumBytes+1;
        network.responses<<feed()<<QJsonDocument(oversized).toJson();updater.check();QTRY_VERIFY(!updater.busy());QVERIFY(!updater.canDownload());
        auto truncated=QJsonDocument::fromJson(manifest(data)).object();truncated["size"]=data.size()+1;
        network.responses<<feed()<<QJsonDocument(truncated).toJson()<<data;
        updater.check();QTRY_VERIFY(!updater.busy());updater.download();QTRY_VERIFY(!updater.busy());
        QCOMPARE(store.version(),QString("1.0.0"));QVERIFY(updater.status().contains(QStringLiteral("长度")));
        network.responses<<QJsonDocument(QJsonArray{}).toJson();updater.check();QTRY_VERIFY(!updater.busy());QVERIFY(!updater.updateAvailable());
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTemporaryDir settings;
    QCoreApplication::setOrganizationName("SerialStudioTest");QCoreApplication::setApplicationName("CatalogUpdates");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    CatalogUpdateTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "catalogupdate_tests.moc"
