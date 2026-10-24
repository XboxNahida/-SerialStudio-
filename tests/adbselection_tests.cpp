#include <QtTest>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include "adbdiscovery.h"

class AdbSelectionTests : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    bool output(const QByteArray &bytes) {
        QFile file(m_dir.filePath("devices.txt"));
        if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;
        return file.write(bytes)==bytes.size();
    }
    void scan(AdbDiscovery &adb) {
        adb.refresh();QTRY_VERIFY_WITH_TIMEOUT(!adb.scanning(),5000);
    }
    bool request(AdbDiscovery &adb,const char *method,const QString &endpoint,const QString &code={}) {
        bool accepted=false;
        if(qstrcmp(method,"pairDevice")==0)
            QMetaObject::invokeMethod(&adb,method,Q_RETURN_ARG(bool,accepted),Q_ARG(QString,endpoint),Q_ARG(QString,code));
        else QMetaObject::invokeMethod(&adb,method,Q_RETURN_ARG(bool,accepted),Q_ARG(QString,endpoint));
        return accepted;
    }
    QByteArray requestLog() {
        QFile file(m_dir.filePath("devices.txt.request"));
        if(!file.open(QIODevice::ReadOnly))return {};
        return file.readAll();
    }
private slots:
    void networkApiIsAvailable() {
        AdbDiscovery adb;
        QVERIFY(adb.metaObject()->indexOfMethod("connectDevice(QString)")>=0);
        QVERIFY(adb.metaObject()->indexOfMethod("pairDevice(QString,QString)")>=0);
        QVERIFY(adb.metaObject()->indexOfMethod("disconnectDevice(QString)")>=0);
        QVERIFY(adb.property("connectionBusy").isValid());
        QVERIFY(adb.property("connectionStatus").isValid());
    }
    void init() {
        QSettings().clear();
        qputenv("SERIALSTUDIO_ADB_SELECTION_PROBE",m_dir.filePath("devices.txt").toUtf8());
        QFile::remove(m_dir.filePath("devices.txt.request"));
        qunsetenv("SERIALSTUDIO_ADB_NETWORK_MODE");
    }
    void cleanup() {qunsetenv("SERIALSTUDIO_ADB_SELECTION_PROBE");qunsetenv("SERIALSTUDIO_ADB_NETWORK_MODE");}
    void networkConnectSelectsExplicitEndpoint() {
        QVERIFY(output("List of devices attached\nboardA device\n192.168.2.8:5555 device\n"));
        AdbDiscovery adb;adb.setExecutablePath(QCoreApplication::applicationFilePath());
        QVERIFY(request(adb,"connectDevice","192.168.2.8:5555"));
        QVERIFY(adb.property("connectionBusy").toBool());
        QVERIFY(!request(adb,"disconnectDevice","192.168.2.8:5555"));
        QTRY_VERIFY_WITH_TIMEOUT(!adb.property("connectionBusy").toBool(),5000);
        QTRY_COMPARE_WITH_TIMEOUT(adb.selectedSerial(),QString("192.168.2.8:5555"),5000);
        QVERIFY(adb.ready());QCOMPARE(requestLog(),QByteArray("connect\n192.168.2.8:5555\n"));
    }
    void networkPairKeepsCodeOutOfArgumentsAndSettings() {
        QVERIFY(output("List of devices attached\nboardA device\n"));
        AdbDiscovery adb;adb.setExecutablePath(QCoreApplication::applicationFilePath());scan(adb);
        QVERIFY(request(adb,"pairDevice","phone.local:37123","123456"));
        QTRY_VERIFY_WITH_TIMEOUT(!adb.property("connectionBusy").toBool(),5000);
        QCOMPARE(requestLog(),QByteArray("pair\nphone.local:37123\nstdin-ok"));
        QCOMPARE(adb.selectedSerial(),QString("boardA"));
        QVERIFY(!adb.property("connectionStatus").toString().contains("123456"));
        for(const auto &key:QSettings().allKeys())QVERIFY(!QSettings().value(key).toString().contains("123456"));
    }
    void networkRejectsInvalidEndpointsAndCodes() {
        AdbDiscovery adb;adb.setExecutablePath(QCoreApplication::applicationFilePath());
        for(const auto &endpoint:QStringList{"","host","-host:55","host:0","host:65536","host:abc","host:5555;whoami","a b:55","host:55\n","999.1.1.1:5555","a..b:55","a.-b:55","[::1]:5555"})
            QVERIFY2(!request(adb,"connectDevice",endpoint),qPrintable(endpoint));
        QVERIFY(!request(adb,"pairDevice","host:5555","12345\n"));
        QVERIFY(!request(adb,"pairDevice","host:5555","abcdef"));
        QVERIFY(!request(adb,"pairDevice","host:5555","123456\n"));
        QVERIFY(requestLog().isEmpty());
    }
    void networkTextFailureAndOutputLimit() {
        AdbDiscovery adb;adb.setExecutablePath(QCoreApplication::applicationFilePath());
        for(const auto &mode:QByteArrayList{"failed","cannot","overflow","exit-error"}) {
            qputenv("SERIALSTUDIO_ADB_NETWORK_MODE",mode);
            QVERIFY(request(adb,"connectDevice","host:5555"));
            QTRY_VERIFY_WITH_TIMEOUT(!adb.property("connectionBusy").toBool(),5000);
            QVERIFY2(adb.property("connectionStatus").toString().contains(QStringLiteral("失败")),qPrintable(adb.property("connectionStatus").toString()));
            QVERIFY(adb.selectedSerial().isEmpty());
        }
    }
    void networkTimeoutAndToolChangeCancelOldTarget() {
        QVERIFY(output("List of devices attached\nboardA device\nhost:5555 device\n"));
        AdbDiscovery adb;adb.setExecutablePath(QCoreApplication::applicationFilePath());
        qputenv("SERIALSTUDIO_ADB_NETWORK_MODE","hang");
        QVERIFY(request(adb,"connectDevice","host:5555"));
        QTRY_VERIFY_WITH_TIMEOUT(!adb.property("connectionBusy").toBool(),18000);
        QVERIFY(adb.property("connectionStatus").toString().contains(QStringLiteral("超时")));
        QVERIFY(request(adb,"connectDevice","host:5555"));
        adb.setExecutablePath(m_dir.filePath("not-existing-adb.exe"));
        QTRY_VERIFY_WITH_TIMEOUT(!adb.property("connectionBusy").toBool(),3000);
        QVERIFY(adb.selectedSerial().isEmpty());
        qunsetenv("SERIALSTUDIO_ADB_NETWORK_MODE");
        adb.setExecutablePath(QCoreApplication::applicationFilePath());scan(adb);
        QVERIFY(adb.selectedSerial().isEmpty());
    }
    void networkDisconnectUsesExplicitEndpoint() {
        QVERIFY(output("List of devices attached\nboardA device\n"));
        AdbDiscovery adb;adb.setExecutablePath(QCoreApplication::applicationFilePath());
        QVERIFY(request(adb,"disconnectDevice","phone.local:5555"));
        QTRY_VERIFY_WITH_TIMEOUT(!adb.property("connectionBusy").toBool(),5000);
        QCOMPARE(requestLog(),QByteArray("disconnect\nphone.local:5555\n"));
    }
    void requestedSelectionSurvivesColdEnumeration() {
        QVERIFY(output("List of devices attached\nboardA device\nboardB device\n"));
        AdbDiscovery adb;adb.setExecutablePath(QCoreApplication::applicationFilePath());
        adb.setSelectedSerial("boardB");
        QVERIFY(!adb.ready());QVERIFY(!adb.enabled());
        scan(adb);QCOMPARE(adb.selectedSerial(),QString("boardB"));QVERIFY(adb.ready());
        adb.setSelectedSerial("boardA");scan(adb);
        QCOMPARE(adb.selectedSerial(),QString("boardA"));
        // 更换工具先清枚举，再收到同一设备列表，也应保留显式目标。
        const auto alternate=m_dir.filePath("adb-copy.exe");
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),alternate));
        adb.setExecutablePath(alternate);QVERIFY(!adb.ready());scan(adb);
        QCOMPARE(adb.selectedSerial(),QString("boardA"));
    }
    void requestedDeviceDoesNotFallBackToAnotherBoard() {
        QVERIFY(output("List of devices attached\nboardA device\nboardB device\n"));
        AdbDiscovery adb;adb.setExecutablePath(QCoreApplication::applicationFilePath());scan(adb);
        adb.setSelectedSerial("boardB");QVERIFY(adb.ready());
        QVERIFY(output("List of devices attached\nboardA device\n"));scan(adb);
        QVERIFY(adb.selectedSerial().isEmpty());QVERIFY(!adb.ready());
        QVERIFY(output("List of devices attached\nboardA device\nboardB device\n"));scan(adb);
        QCOMPARE(adb.selectedSerial(),QString("boardB"));
    }
    void previewPathChangesDoNotWriteLegacySettings() {
        QSettings().setValue("terminal/adbPath","原路径");
        AdbDiscovery adb;
        QVERIFY(adb.setProperty("persistenceEnabled",false));
        adb.setExecutablePath(QCoreApplication::applicationFilePath());
        QCOMPARE(QSettings().value("terminal/adbPath").toString(),QStringLiteral("原路径"));
        QVERIFY(!adb.enabled());QVERIFY(!adb.scanning());
    }
    void defaultPathPersistenceRemainsCompatible() {
        AdbDiscovery adb;adb.setExecutablePath(QCoreApplication::applicationFilePath());
        QCOMPARE(QSettings().value("terminal/adbPath").toString(),QCoreApplication::applicationFilePath());
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const auto arguments=app.arguments();
    if(arguments.size()>2&&QStringList{"connect","pair","disconnect"}.contains(arguments[1])) {
        QFile log(QString::fromUtf8(qgetenv("SERIALSTUDIO_ADB_SELECTION_PROBE"))+".request");
        if(!log.open(QIODevice::WriteOnly|QIODevice::Truncate))return 2;
        for(int i=1;i<arguments.size();++i)log.write(arguments[i].toUtf8()+"\n");
        if(arguments[1]=="pair") {
            QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 3;
            if(input.readLine().trimmed()!="123456")return 4;
            log.write("stdin-ok");
        }
        log.close();
        const auto mode=qgetenv("SERIALSTUDIO_ADB_NETWORK_MODE");
        if(mode=="hang")QThread::sleep(30);
        QByteArray result="connected";
        if(mode=="failed")result="failed to connect";
        else if(mode=="cannot")result="cannot connect";
        else if(mode=="overflow")result=QByteArray(70000,'x');
        else if(arguments[1]=="pair")result="Successfully paired 123456";
        fwrite(result.constData(),1,size_t(result.size()),stdout);fflush(stdout);
        return mode=="exit-error"?1:0;
    }
    if(app.arguments().contains("devices")) {
        QFile file(QString::fromUtf8(qgetenv("SERIALSTUDIO_ADB_SELECTION_PROBE")));
        if(!file.open(QIODevice::ReadOnly))return 1;
        const auto bytes=file.readAll();fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);return 0;
    }
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    app.setOrganizationName("SerialStudioTests");app.setApplicationName("AdbSelectionTests");
    AdbSelectionTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "adbselection_tests.moc"
