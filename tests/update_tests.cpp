#include <QtTest>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSettings>
#include <QTemporaryDir>
#include "updatecontroller.h"

class ProbeReply final : public QNetworkReply {
public:
    ProbeReply(const QNetworkRequest &request, QByteArray bytes, QObject *parent)
        : QNetworkReply(parent), payload(std::move(bytes)) {
        setRequest(request);setUrl(request.url());setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute,200);open(ReadOnly);
        QTimer::singleShot(0,this,[this]{emit readyRead();setFinished(true);emit finished();});
    }
    void abort() override {setError(OperationCanceledError,"cancelled");}
    qint64 bytesAvailable() const override {return payload.size()-position+QNetworkReply::bytesAvailable();}
protected:
    qint64 readData(char *data,qint64 max) override {
        const auto size=qMin(max,qint64(payload.size())-position);
        if(size<=0)return -1;
        memcpy(data,payload.constData()+position,size_t(size));position+=size;return size;
    }
private:
    QByteArray payload;qint64 position=0;
};
class ProbeNetwork final : public QNetworkAccessManager {
public:
    QList<QByteArray> responses;QStringList urls;
protected:
    QNetworkReply *createRequest(Operation,const QNetworkRequest &request,QIODevice *) override {
        urls<<request.url().toString();
        return new ProbeReply(request,responses.isEmpty()?QByteArray():responses.takeFirst(),this);
    }
};
class UpdateTests : public QObject {
    Q_OBJECT
public:
    static int liveDownload(QCoreApplication &app) {
        QTemporaryDir cache(QDir(QCoreApplication::applicationDirPath()).filePath("remote-update-probe-XXXXXX"));
        if(!cache.isValid())return 2;
        UpdateController checker("0.4.1-beta.1");checker.m_cacheRoot=cache.path();
        bool downloading=false;int result=1;
        QObject::connect(&checker,&UpdateController::changed,&app,[&]{
            if(checker.busy())return;
            if(!downloading&&checker.canDownload()){
                downloading=true;QTimer::singleShot(0,&checker,&UpdateController::download);return;
            }
            if(checker.downloadReady()){
                qInfo().noquote()<<"REMOTE_DOWNLOAD_VERIFIED"<<checker.latestVersion()<<QFileInfo(checker.m_downloadPath).size()<<checker.m_expectedHash;
                result=0;
            } else qWarning().noquote()<<checker.status();
            app.quit();
        });
        QTimer::singleShot(360000,&app,&QCoreApplication::quit);
        QTimer::singleShot(0,&checker,[&]{checker.check(true);});
        app.exec();return result;
    }
private:
    QJsonObject release(const QString &version,bool beta=true) {
        const auto name="SerialStudio-"+version+"-Windows-x64-Setup.exe";
        const auto base="https://github.com/XboxNahida/-SerialStudio-/releases/download/v"+version+"/";
        return {{"tag_name","v"+version},{"name","测试版"},{"body","更新内容"},{"draft",false},{"prerelease",beta},
            {"html_url","https://github.com/XboxNahida/-SerialStudio-/releases/tag/v"+version},
            {"assets",QJsonArray{QJsonObject{{"name",name},{"size",9},{"browser_download_url",base+name}},
                                 QJsonObject{{"name",name+".sha256"},{"browser_download_url",base+name+".sha256"}}}}};
    }
    QByteArray feed(const QJsonArray &items){return QJsonDocument(items).toJson();}
private slots:
    void versionOrder() {
        QVERIFY(UpdateController::compareVersions("1.0.0","0.4.2-beta.1")>0);
        QVERIFY(UpdateController::compareVersions("1.0.0","1.0.0-rc.9")>0);
        QVERIFY(UpdateController::compareVersions("1.0.1","1.0.0")>0);
        QVERIFY(UpdateController::compareVersions("0.4.2-beta.10","0.4.2-beta.2")>0);
        QVERIFY(UpdateController::compareVersions("0.4.2","0.4.2-rc.9")>0);
        QVERIFY(UpdateController::compareVersions("0.4.2-rc.1","0.4.2-beta.10")>0);
        QVERIFY(UpdateController::compareVersions("0.4.10","0.4.2")>0);
        QCOMPARE(UpdateController::compareVersions("broken","0.4.2"),0);
    }
    void stableOneDoesNotDowngradeOrSelectBeta() {
        ProbeNetwork network;UpdateController stable("1.0.0",&network);
        network.responses<<feed({release("0.4.2-beta.1"),release("1.0.1-beta.1"),release("1.0.0",false)});
        stable.check(true);QTRY_VERIFY(!stable.busy());QVERIFY(!stable.updateAvailable());QVERIFY(!stable.canDownload());
        network.responses<<feed({release("1.0.1-beta.1"),release("1.0.1",false)});
        stable.check(true);QTRY_VERIFY(!stable.busy());QVERIFY(stable.updateAvailable());QCOMPARE(stable.latestVersion(),QString("1.0.1"));
    }
    void channelsAndIgnore() {
        ProbeNetwork network;UpdateController checker("0.4.2-beta.1",&network);
        network.responses<<feed({release("0.4.3-beta.1"),release("0.4.2",false)});
        checker.check(false);QTRY_VERIFY(!checker.busy());
        QCOMPARE(checker.latestVersion(),QString("0.4.3-beta.1"));QVERIFY(checker.updateAvailable());
        checker.ignore();QVERIFY(!checker.updateAvailable());
        network.responses<<feed({release("0.4.3-beta.1")});checker.check(false);QTRY_VERIFY(!checker.busy());QVERIFY(!checker.updateAvailable());
        network.responses<<feed({release("0.4.3-beta.1")});checker.check(true);QTRY_VERIFY(!checker.busy());QVERIFY(checker.updateAvailable());
        UpdateController stable("0.4.1",&network);
        network.responses<<feed({release("0.4.3-beta.1"),release("0.4.2",false)});
        stable.check(true);QTRY_VERIFY(!stable.busy());QCOMPARE(stable.latestVersion(),QString("0.4.2"));
    }
    void rejectsForeignAssets() {
        ProbeNetwork network;UpdateController checker("0.4.1-beta.1",&network);
        auto item=release("0.4.2-beta.1");item["html_url"]="https://github.com/OtherOwner/app/releases/tag/v0.4.2-beta.1";
        network.responses<<feed({item});checker.check();QTRY_VERIFY(!checker.busy());QVERIFY(!checker.updateAvailable());
        item=release("0.4.2-beta.1");auto assets=item["assets"].toArray();auto asset=assets[0].toObject();
        asset["browser_download_url"]="https://example.com/setup.exe";assets[0]=asset;item["assets"]=assets;
        network.responses<<feed({item});checker.check();QTRY_VERIFY(!checker.busy());QVERIFY(!checker.canDownload());
    }
    void downloadAndTampering() {
        QTemporaryDir directory;ProbeNetwork network;UpdateController checker("0.4.1-beta.1",&network);
        checker.m_cacheRoot=directory.path();
        network.responses<<feed({release("0.4.2-beta.1")});checker.check();QTRY_VERIFY(!checker.busy());
        const QByteArray bytes="installer";const auto hash=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
        network.responses<<(hash+"  SerialStudio-0.4.2-beta.1-Windows-x64-Setup.exe\n")<<bytes;
        checker.download();QTRY_VERIFY(!checker.busy());QVERIFY(checker.downloadReady());
        QSignalSpy exit(&checker,&UpdateController::exitRequested);bool launched=false;
        checker.m_launch=[&](const QString &,const QStringList &){launched=true;return false;};
        checker.install();QVERIFY(launched);QCOMPARE(exit.count(),0);
        QFile file(checker.m_downloadPath);QVERIFY(file.open(QIODevice::Append));file.write("corrupt");file.close();
        launched=false;checker.install();QVERIFY(!launched);QVERIFY(!checker.downloadReady());
        network.responses<<(hash+"  SerialStudio-0.4.2-beta.1-Windows-x64-Setup.exe\n")<<bytes;
        checker.download();QTRY_VERIFY(!checker.busy());QVERIFY(checker.downloadReady());
        checker.m_launch=[&](const QString &program,const QStringList &args){
            launched=true;return program==checker.m_downloadPath&&args==QStringList{"/UPDATEPID="+QString::number(QCoreApplication::applicationPid())};};
        checker.install();QVERIFY(launched);QCOMPARE(exit.count(),1);
        network.responses<<(QByteArray(64,'0')+"  SerialStudio-0.4.2-beta.1-Windows-x64-Setup.exe\n")<<bytes;
        checker.download();QTRY_VERIFY(!checker.busy());QVERIFY(!checker.downloadReady());QVERIFY(checker.status().contains("校验"));
    }
    void uninstallGuard() {
        ProbeNetwork network;UpdateController checker("0.4.2-beta.1",&network);
        QVERIFY(!checker.uninstallAvailable());bool launched=false;
        checker.m_launch=[&](const QString &,const QStringList &){launched=true;return true;};
        checker.uninstall();QVERIFY(!launched);
    }
    void malformedAndOversize() {
        ProbeNetwork network;UpdateController checker("0.4.1-beta.1",&network);
        network.responses<<QByteArray("not json");checker.check();QTRY_VERIFY(!checker.busy());QVERIFY(!checker.updateAvailable());
        auto item=release("0.4.2-beta.1");auto assets=item["assets"].toArray();auto asset=assets[0].toObject();
        asset["size"]=1024*1024*512;assets[0]=asset;item["assets"]=assets;
        network.responses<<feed({item});checker.check();QTRY_VERIFY(!checker.busy());QVERIFY(!checker.canDownload());
    }
    void staleCacheOnly() {
        QTemporaryDir cache;ProbeNetwork network;UpdateController checker("0.4.2-beta.1",&network);
        checker.m_cacheRoot=cache.path();
        const auto directory=cache.filePath("11111111-1111-1111-1111-111111111111");QVERIFY(QDir().mkpath(directory));
        const auto stale=directory+"/SerialStudio-0.4.1-Windows-x64-Setup.exe";
        const auto recent=directory+"/SerialStudio-0.4.2-beta.1-Windows-x64-Setup.exe";
        for(const auto &path:{stale,recent,directory+"/用户文件.txt"}){
            QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("test");
            if(path==stale)QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addDays(-8),QFileDevice::FileModificationTime));
        }
        checker.pruneCache();QVERIFY(!QFile::exists(stale));QVERIFY(QFile::exists(recent));QVERIFY(QFile::exists(directory+"/用户文件.txt"));
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTemporaryDir settings;
    QCoreApplication::setOrganizationName("SerialStudioTest");QCoreApplication::setApplicationName("Updates");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    if(app.arguments().contains("--live-download"))return UpdateTests::liveDownload(app);
    UpdateTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "update_tests.moc"
