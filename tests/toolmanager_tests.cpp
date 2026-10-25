#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "toolmanager.h"
#include "toolpaths.h"
#include "settingsstore.h"
#include <QNetworkReply>

class ArchiveReply: public QNetworkReply {
    QByteArray m_bytes;qint64 m_position=0;
public:
    ArchiveReply(const QNetworkRequest &request,QByteArray data,QObject *parent):QNetworkReply(parent),m_bytes(std::move(data)) {
        setRequest(request);setUrl(request.url());setAttribute(QNetworkRequest::HttpStatusCodeAttribute,200);open(ReadOnly);
        QTimer::singleShot(0,this,[this]{if(isFinished())return;emit readyRead();if(!isFinished()){setFinished(true);emit finished();}});
    }
    void abort() override {if(!isFinished()){setError(OperationCanceledError,"cancelled");setFinished(true);emit finished();}}
    qint64 bytesAvailable() const override {return m_bytes.size()-m_position+QNetworkReply::bytesAvailable();}
protected:
    qint64 readData(char *data,qint64 maximum) override {const auto count=qMin(maximum,qint64(m_bytes.size())-m_position);if(count<=0)return -1;memcpy(data,m_bytes.constData()+m_position,size_t(count));m_position+=count;return count;}
};
class ArchiveNetwork: public QNetworkAccessManager {
public: QByteArray bytes="fixture";QUrl requested;
protected:
    QNetworkReply *createRequest(Operation,const QNetworkRequest &request,QIODevice *) override {requested=request.url();return new ArchiveReply(request,bytes,this);}
};
class ToolManagerTests: public QObject {
    Q_OBJECT
private slots:
    void missingToolsRespectSelectedPaths() {
        QTemporaryDir dir;SettingsStore settings(dir.filePath("config"));ToolManager manager(&settings,nullptr,dir.filePath("tools"));
        const auto absent=dir.filePath("not-installed.exe");
        const auto rows=manager.missingTools({"adb","sftp"},{{"adb",absent},{"sftp",absent}});
        QCOMPARE(rows.size(),2);QCOMPARE(rows[0].toMap().value("componentId").toString(),QString("android-platform-tools"));
        QCOMPARE(rows[1].toMap().value("componentId").toString(),QString("openssh"));
        const auto program=QCoreApplication::applicationFilePath();
        QVERIFY(manager.missingTools({"adb","sftp"},{{"adb",program},{"sftp",program}}).isEmpty());
        QVERIFY(manager.missingTools({}).isEmpty());
        QVERIFY(!manager.busy());
    }
    void failedDigestSizeAndCancelNeverInstall() {
        for(const auto &scenario:{"hash","size","cancel"}) {
            QTemporaryDir d;SettingsStore settings(d.filePath("config"));ArchiveNetwork network;
            ToolManager manager(&settings,nullptr,d.filePath("tools"),&network);
            QFile python(d.filePath("python.exe"));QVERIFY(python.open(QIODevice::WriteOnly));python.write("fixture");python.close();
            manager.setPath("python",python.fileName());
            auto component=manager.m_catalog.first().toMap();component["bytes"]=QString(scenario)=="size"?1:7;
            component["sha256"]=QString(64,'0');manager.m_catalog[0]=component;
            QVERIFY(manager.install("android-platform-tools"));
            if(QString(scenario)=="cancel")manager.cancel();
            QTRY_VERIFY(!manager.busy());
            QVERIFY(!QFileInfo::exists(d.filePath("tools/android-platform-tools")));
            QVERIFY(QDir(d.filePath("tools")).entryList({".install-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
            if(QString(scenario)=="hash")QVERIFY(manager.status().contains("SHA256"));
            QCOMPARE(network.requested.scheme(),QString("https"));
        }
    }
    void verifiedDownloadUnpacksAndCommits() {
        const auto python=QCoreApplication::applicationDirPath()+"/toolchain-audit/python/python.exe";
        if(!QFileInfo::exists(python))QSKIP("本机离线 Python 验收输入不存在；先准备工具链再运行此集成项");
        QTemporaryDir d;SettingsStore settings(d.filePath("config"));ArchiveNetwork network;
        network.bytes=QByteArray::fromBase64("UEsDBBQAAAAAAMGGWF3uQOUFBwAAAAcAAAAIAAAAdG9vbC5leGVmaXh0dXJlUEsBAhQAFAAAAAAAwYZYXe5A5QUHAAAABwAAAAgAAAAAAAAAAAAAAIABAAAAAHRvb2wuZXhlUEsFBgAAAAABAAEANgAAAC0AAAAAAA==");
        ToolManager manager(&settings,nullptr,d.filePath("tools"),&network);manager.setPath("python",python);
        auto component=manager.m_catalog.first().toMap();component["bytes"]=network.bytes.size();
        component["sha256"]=QString::fromLatin1(QCryptographicHash::hash(network.bytes,QCryptographicHash::Sha256).toHex());
        component["entrypoints"]=QStringList{"tool.exe"};manager.m_catalog[0]=component;
        QVERIFY(manager.install("android-platform-tools"));QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),10000);
        const auto output=d.filePath("tools/android-platform-tools/37.0.1/tool.exe");
        QFile installed(output);QVERIFY2(installed.open(QIODevice::ReadOnly),qPrintable(manager.status()));QCOMPARE(installed.readAll(),QByteArray("fixture"));
        QVERIFY(QFileInfo::exists(QFileInfo(output).absolutePath()+"/.serialstudio-files.json"));
    }
    void interpreterFailureReportsPathAndExitCode() {
        QTemporaryDir d;SettingsStore settings(d.filePath("config"));ArchiveNetwork network;
        network.bytes=QByteArray::fromBase64("UEsDBBQAAAAAAMGGWF3uQOUFBwAAAAcAAAAIAAAAdG9vbC5leGVmaXh0dXJlUEsBAhQAFAAAAAAAwYZYXe5A5QUHAAAABwAAAAgAAAAAAAAAAAAAAIABAAAAAHRvb2wuZXhlUEsFBgAAAAABAAEANgAAAC0AAAAAAA==");
        ToolManager manager(&settings,nullptr,d.filePath("tools"),&network);
        // 本测试程序无法执行 Python 脚本，收到 -I 会以退出码 1 结束。
        const auto interpreter=QCoreApplication::applicationFilePath();
        QVERIFY(manager.setPath("python",interpreter));
        auto component=manager.m_catalog.first().toMap();component["bytes"]=network.bytes.size();
        component["sha256"]=QString::fromLatin1(QCryptographicHash::hash(network.bytes,QCryptographicHash::Sha256).toHex());
        component["entrypoints"]=QStringList{"tool.exe"};manager.m_catalog[0]=component;
        QVERIFY(manager.install("android-platform-tools"));
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),10000);
        QVERIFY2(manager.status().contains(interpreter),qPrintable(manager.status()));
        QVERIFY2(manager.status().contains(QStringLiteral("退出码 1")),qPrintable(manager.status()));
        QVERIFY(!QFileInfo::exists(d.filePath("tools/android-platform-tools/37.0.1")));
    }
    void seedPreservesExistingVersionsAndRejectsTamperedSeed() {
        QTemporaryDir d;const auto source=d.filePath("seed/python/3.13.7");QDir().mkpath(source);
        QFile program(source+"/python.exe");QVERIFY(program.open(QIODevice::WriteOnly));program.write("fixture");program.close();
        const auto digest=QString::fromLatin1(QCryptographicHash::hash("fixture",QCryptographicHash::Sha256).toHex());
        QFile receipt(source+"/.serialstudio-files.json");QVERIFY(receipt.open(QIODevice::WriteOnly));
        receipt.write(QJsonDocument(QJsonArray{QJsonObject{{"path","python.exe"},{"sha256",digest}}}).toJson());receipt.close();
        QVERIFY(ToolManager::seedManaged(d.filePath("seed"),d.filePath("tools")));
        QVERIFY(QFileInfo::exists(d.filePath("tools/python/3.13.7/python.exe")));
        QFile existing(d.filePath("tools/python/3.13.7/personal.txt"));QVERIFY(existing.open(QIODevice::WriteOnly));existing.write("keep");existing.close();
        QVERIFY(ToolManager::seedManaged(d.filePath("seed"),d.filePath("tools")));
        QVERIFY(QFileInfo::exists(existing.fileName()));
        QVERIFY(program.open(QIODevice::WriteOnly));program.write("tampered");program.close();
        QVERIFY(!ToolManager::seedManaged(d.filePath("seed"),d.filePath("second")));
        QVERIFY(!QFileInfo::exists(d.filePath("second/python/3.13.7/python.exe")));
    }
    void overridesPersistAndUnsupportedDownloadsAreRejected() {
        QTemporaryDir d;SettingsStore settings(d.filePath("config"));
        ToolManager manager(&settings,nullptr,d.filePath("tools"));
        QVERIFY(manager.components().size()>=7);
        QVERIFY(manager.setPath("ssh",d.filePath("missing.exe")));
        QVERIFY(ToolPaths::resolve("ssh").isEmpty());
        QCOMPARE(settings.value("tools/paths").toMap().value("ssh").toString(),d.filePath("missing.exe"));
        QVERIFY(!manager.install("unknown"));QVERIFY(!manager.install("jlink"));QVERIFY(!manager.busy());
        QVERIFY(manager.setPath("ssh",""));QVERIFY(!settings.value("tools/paths").toMap().contains("ssh"));
    }
    void reinstallRepairsMissingFilesAndPreservesUserChanges() {
        QTemporaryDir d;const auto source=d.filePath("seed/python/3.13.7");QDir().mkpath(source);
        const auto digest=QString::fromLatin1(QCryptographicHash::hash("fixture",QCryptographicHash::Sha256).toHex());
        QJsonArray entries;
        for(const auto &name:{"python.exe","LICENSE.txt"}) {
            QFile file(source+'/'+name);QVERIFY(file.open(QIODevice::WriteOnly));file.write("fixture");
            entries.append(QJsonObject{{"path",name},{"sha256",digest}});
        }
        QFile receipt(source+"/.serialstudio-files.json");QVERIFY(receipt.open(QIODevice::WriteOnly));
        receipt.write(QJsonDocument(entries).toJson());receipt.close();
        const auto root=d.filePath("tools"),version=root+"/python/3.13.7";
        QVERIFY(ToolManager::seedManaged(d.filePath("seed"),root));
        QFile modified(version+"/LICENSE.txt");QVERIFY(modified.open(QIODevice::WriteOnly));modified.write("modified");modified.close();
        QFile unknown(version+"/personal.txt");QVERIFY(unknown.open(QIODevice::WriteOnly));unknown.write("personal");unknown.close();
        QVERIFY(ToolManager::removeManaged(root));
        QVERIFY(!QFileInfo::exists(version+"/python.exe"));
        QVERIFY(ToolManager::seedManaged(d.filePath("seed"),root));
        QFile restored(version+"/python.exe");QVERIFY(restored.open(QIODevice::ReadOnly));QCOMPARE(restored.readAll(),QByteArray("fixture"));restored.close();
        QVERIFY(modified.open(QIODevice::ReadOnly));QCOMPARE(modified.readAll(),QByteArray("modified"));modified.close();
        QVERIFY(ToolManager::removeManaged(root));
        QVERIFY(!QFileInfo::exists(version+"/python.exe"));
        QVERIFY(QFileInfo::exists(unknown.fileName()));QVERIFY(QFileInfo::exists(modified.fileName()));
    }
    void uninstallOnlyUnchangedManifestFiles() {
        QTemporaryDir d;const auto version=d.filePath("tools/python/3.13.7");QDir().mkpath(version);
        for(const auto &name:{"known.exe","changed.exe","personal.txt"}){QFile f(version+'/'+name);QVERIFY(f.open(QIODevice::WriteOnly));f.write("fixture");}
        const auto digest=QString::fromLatin1(QCryptographicHash::hash("fixture",QCryptographicHash::Sha256).toHex());
        QJsonArray receipt{QJsonObject{{"path","known.exe"},{"sha256",digest}},QJsonObject{{"path","changed.exe"},{"sha256",QString(64,'0')}},QJsonObject{{"path","../../outside.txt"},{"sha256",digest}}};
        QFile f(version+"/.serialstudio-files.json");QVERIFY(f.open(QIODevice::WriteOnly));f.write(QJsonDocument(receipt).toJson());f.close();
        QVERIFY(ToolManager::removeManaged(d.filePath("tools")));
        QVERIFY(!QFileInfo::exists(version+"/known.exe"));QVERIFY(QFileInfo::exists(version+"/changed.exe"));QVERIFY(QFileInfo::exists(version+"/personal.txt"));
    }
};
QTEST_GUILESS_MAIN(ToolManagerTests)
#include "toolmanager_tests.moc"
