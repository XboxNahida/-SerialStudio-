#include <QtTest>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
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
private slots:
    void init() {
        QSettings().clear();
        qputenv("SERIALSTUDIO_ADB_SELECTION_PROBE",m_dir.filePath("devices.txt").toUtf8());
    }
    void cleanup() {qunsetenv("SERIALSTUDIO_ADB_SELECTION_PROBE");}
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
