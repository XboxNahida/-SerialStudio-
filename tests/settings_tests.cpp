#include <QtTest>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QTemporaryDir>
#include "settingsstore.h"
#include "quickcommands.h"

static QByteArray readFile(const QString &path)
{
    QFile file(path); if(!file.open(QIODevice::ReadOnly))return {}; return file.readAll();
}
static QString temporaryTemplate()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("settings-test-XXXXXX");
}
class SettingsTests : public QObject {
    Q_OBJECT
private slots:
    void persistChineseAndDestructor()
    {
        QTemporaryDir dir(temporaryTemplate()); QVERIFY(dir.isValid());
        { SettingsStore store(dir.path());
          QVERIFY(store.setValue("serial/draft",QStringLiteral("你好，开发板\n")));
          QVERIFY(store.setValue("serial/config",QVariantMap{{"baud",115200},{"hex",false}}));
          QVERIFY(store.flush()); }
        const auto bytes=readFile(dir.filePath("settings.json"));
        QVERIFY(bytes.contains(QStringLiteral("你好").toUtf8()));
        QCOMPARE(QJsonDocument::fromJson(bytes).object().value("schemaVersion").toInt(),1);
        { SettingsStore store(dir.path());
          QCOMPARE(store.value("serial/draft").toString(),QStringLiteral("你好，开发板\n"));
          QCOMPARE(store.value("serial/config").toMap().value("baud").toInt(),115200);
          QCOMPARE(store.value("missing",42).toInt(),42);
          QVERIFY(store.setValue("dark",false)); }
        SettingsStore reopened(dir.path()); QCOMPARE(reopened.value("dark").toBool(),false);
    }
    void delayedWriteAndBadFile()
    {
        QTemporaryDir dir(temporaryTemplate()); SettingsStore store(dir.path());
        QVERIFY(store.setValue("draft","first")); QVERIFY(store.setValue("draft","last"));
        QVERIFY(!QFile::exists(dir.filePath("settings.json")));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(dir.filePath("settings.json")),1500);
        SettingsStore reopened(dir.path()); QCOMPARE(reopened.value("draft").toString(),QString("last"));
        QTemporaryDir broken(temporaryTemplate()); QFile file(broken.filePath("settings.json"));
        QVERIFY(file.open(QIODevice::WriteOnly)); const QByteArray original="{损坏的配置";
        file.write(original); file.close();
        { SettingsStore damaged(broken.path()); QVERIFY(!damaged.error().isEmpty());
          QVERIFY(!damaged.setValue("draft","replacement")); QVERIFY(!damaged.flush()); }
        QCOMPARE(readFile(file.fileName()),original);
    }
    void protectedSecrets()
    {
        QTemporaryDir dir(temporaryTemplate()); const QString password=QStringLiteral("保密口令-985635");
        { SettingsStore store(dir.path()); QVERIFY(store.setSecret("mqtt/password",password));
          QCOMPARE(store.secret("mqtt/password"),password); QVERIFY(store.flush()); }
        const auto bytes=readFile(dir.filePath("settings.json"));
        QVERIFY(!bytes.contains(password.toUtf8())); QVERIFY(!bytes.contains(password.toUtf8().toBase64()));
        SettingsStore reopened(dir.path()); QCOMPARE(reopened.secret("mqtt/password"),password);
        QVERIFY(reopened.setSecret("mqtt/password",{})); QVERIFY(reopened.secret("mqtt/password").isEmpty());
        QVERIFY(reopened.flush());
    }
    void commandsEditDeleteAndScopes()
    {
        QTemporaryDir dir(temporaryTemplate()); SettingsStore store(dir.path()); QuickCommands commands(&store);
        const auto id=commands.put("serial",{{"title",QStringLiteral("读取温度")},{"text","TEMP?"},
                                  {"vendor",QVariantMap{{"model",QStringLiteral("开发板")}}}});
        QVERIFY(!id.isEmpty()); QCOMPARE(commands.entries("serial").size(),1);
        QVERIFY(commands.entries("network").isEmpty());
        QCOMPARE(commands.put("serial",{{"id",id},{"title",QStringLiteral("更新名称")},{"text","TEMP 2?"}}),id);
        QCOMPARE(commands.entries("serial").size(),1);
        QCOMPARE(commands.entries("serial").first().toMap().value("vendor").toMap().value("model").toString(),QStringLiteral("开发板"));
        QVERIFY(commands.put("unsupported",{{"title","x"},{"text","x"}}).isEmpty());
        QVERIFY(commands.put("serial",{{"title","x"},{"text",23}}).isEmpty());
        QVERIFY(commands.put("serial",{{"id","bad-id"},{"title","x"},{"text","x"}}).isEmpty());
        QVERIFY(store.flush());
        SettingsStore reopened(dir.path()); QuickCommands restored(&reopened);
        QCOMPARE(restored.entries("serial").size(),1); QCOMPARE(restored.entries("serial").first().toMap().value("id").toString(),id);
        QVERIFY(restored.remove("serial",id)); QVERIFY(!restored.remove("serial",id)); QVERIFY(reopened.flush());
        SettingsStore finalStore(dir.path()); QuickCommands finalCommands(&finalStore); QVERIFY(finalCommands.entries("serial").isEmpty());
    }
    void builtinFavoriteMetadataSurvivesRestart()
    {
        QTemporaryDir dir(temporaryTemplate()); QString id;
        {
            SettingsStore store(dir.path()); QuickCommands commands(&store);
            id=commands.put("terminal-linux",{{"builtinId","linux-legacy-01"},{"title",QStringLiteral("列出目录")},
                {"text","ls -alh /tmp"},{"favorite",true},{"interaction","shell"}});
            QVERIFY(!id.isEmpty()); QVERIFY(store.flush());
        }
        SettingsStore reopened(dir.path()); QuickCommands restored(&reopened);
        QCOMPARE(restored.entries("terminal-linux").size(),1);
        const auto entry=restored.entries("terminal-linux").first().toMap();
        QCOMPARE(entry.value("builtinId").toString(),QString("linux-legacy-01"));
        QVERIFY(entry.value("favorite").toBool()); QCOMPARE(entry.value("id").toString(),id);
        QVERIFY(restored.remove("terminal-linux",id)); QVERIFY(reopened.flush());
        SettingsStore again(dir.path()); QuickCommands finalCommands(&again);
        QVERIFY(finalCommands.entries("terminal-linux").isEmpty());
    }
    void commandImportAtomicAndExport()
    {
        QTemporaryDir dir(temporaryTemplate()); SettingsStore store(dir.path()); QuickCommands commands(&store);
        QVERIFY(store.setSecret("mqtt/password",QStringLiteral("导出禁止出现")));
        const auto id=commands.put("mqtt",{{"title",QStringLiteral("心跳")},{"text","ping"},{"qos",1}});
        QVERIFY(!id.isEmpty());
        const auto url=QUrl::fromLocalFile(dir.filePath("快捷中文.json"));
        QVERIFY(commands.exportFile("mqtt",url));
        QVERIFY(!readFile(url.toLocalFile()).contains(QStringLiteral("导出禁止出现").toUtf8()));
        QTemporaryDir other(temporaryTemplate()); SettingsStore target(other.path()); QuickCommands imported(&target);
        QVERIFY(imported.importFile("mqtt",url)); QCOMPARE(imported.entries("mqtt"),commands.entries("mqtt"));
        QVERIFY(imported.importFile("mqtt",url)); QCOMPARE(imported.entries("mqtt").size(),1);
        QVERIFY(!imported.importFile("http",url));
        QFile invalid(dir.filePath("invalid.json")); QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("{\"schemaVersion\":1,\"scope\":\"mqtt\",\"entries\":[{\"title\":\"valid\",\"text\":\"x\"},{\"title\":false,\"text\":\"bad\"}]}"); invalid.close();
        const auto before=imported.entries("mqtt");
        QVERIFY(!imported.importFile("mqtt",QUrl::fromLocalFile(invalid.fileName())));
        QCOMPARE(imported.entries("mqtt"),before);
        QVERIFY(!imported.exportFile("mqtt",QUrl("https://example.com/a")));
    }
    void tenThousandCommandsBatched()
    {
        QTemporaryDir dir(temporaryTemplate()); QString lastId;
        { SettingsStore store(dir.path()); QuickCommands commands(&store); QElapsedTimer elapsed; elapsed.start();
          for(int i=0;i<10000;++i) {
              lastId=commands.put("terminal-linux",{{"title",QString::number(i)},{"text",QStringLiteral("echo 中文 %1").arg(i)}});
              QVERIFY(!lastId.isEmpty());
          }
          QVERIFY(!QFile::exists(dir.filePath("settings.json")));
          QCOMPARE(commands.entries("terminal-linux").size(),10000);
          QVERIFY(store.flush()); qInfo()<<"10000 commands persisted in milliseconds:"<<elapsed.elapsed(); }
        SettingsStore reopened(dir.path()); QuickCommands commands(&reopened);
        QCOMPARE(commands.entries("terminal-linux").size(),10000);
        QCOMPARE(commands.entries("terminal-linux").last().toMap().value("id").toString(),lastId);
    }
    void allScopesAndDuplicateImport()
    {
        QTemporaryDir dir(temporaryTemplate()); SettingsStore store(dir.path()); QuickCommands commands(&store);
        for(const auto &scope:QStringList{"serial","network","http","mqtt","terminal-linux","terminal-powershell",
                        "terminal-cmd","terminal-android","board-i2c","board-spi","board-usb"})
            QVERIFY(!commands.put(scope,{{"title",scope},{"text","hello"}}).isEmpty());
        const auto one=commands.entries("serial").first().toMap();
        QFile file(dir.filePath("duplicate.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"schemaVersion",1},{"scope","serial"},
                   {"entries",QJsonArray::fromVariantList({one,one})}}).toJson());file.close();
        QVERIFY(!commands.importFile("serial",QUrl::fromLocalFile(file.fileName())));
        QCOMPARE(commands.entries("serial").size(),1);
        QVERIFY(commands.put("serial",{{"title","bad"},{"text","x"},{"extra",QUrl("https://example.com")}}).isEmpty());
        QVERIFY(commands.put("serial",{{"title","bad"},{"text","x"},{"hex","yes"}}).isEmpty());
    }
    void pendingCommandsWhenStoreDestroyedFirst()
    {
        QTemporaryDir dir(temporaryTemplate()); auto *store=new SettingsStore(dir.path());
        QuickCommands commands(store);
        QVERIFY(!commands.put("http",{{"title","draft"},{"text","body"}}).isEmpty());
        delete store;
        SettingsStore reopened(dir.path()); QuickCommands restored(&reopened);
        QCOMPARE(restored.entries("http").size(),1);
        QVERIFY(commands.put("http",{{"title","bad"},{"text","x"}}).isEmpty());
    }
    void legacyMigrationIsolatedInWorkspace()
    {
        QTemporaryDir dir(temporaryTemplate()); QVERIFY(dir.isValid());
        const auto previousOrg=QCoreApplication::organizationName();
        const auto previousApp=QCoreApplication::applicationName();
        const auto previousFormat=QSettings::defaultFormat();
        const auto restore=qScopeGuard([=]{QCoreApplication::setOrganizationName(previousOrg);
            QCoreApplication::setApplicationName(previousApp);QSettings::setDefaultFormat(previousFormat);});
        const auto base=QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        // 先检查解析后路径，确保测试不会在真实用户配置目录创建任何文件。
        QCoreApplication::setOrganizationName(QDir(base).relativeFilePath(dir.path()));
        QCoreApplication::setApplicationName("migration");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,base);
        const auto target=QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        QSettings legacy;
        QVERIFY2(target.startsWith(QDir::cleanPath(dir.path())+'/'),qPrintable(target));
        QVERIFY2(QDir::cleanPath(legacy.fileName()).startsWith(QDir::cleanPath(dir.path())+'/'),qPrintable(legacy.fileName()));
        legacy.setValue("dark",false);legacy.setValue("connection",QVariantMap{{"baud",9600}});
        legacy.setValue("terminal/adbPath",QStringLiteral("工具/adb.exe"));legacy.setValue("updates/ignoredTag","v0.4.2");
        legacy.setValue("unrelated","ignored");legacy.sync();QCOMPARE(legacy.status(),QSettings::NoError);
        const auto original=readFile(legacy.fileName());
        { SettingsStore isolated(dir.filePath("isolated")); QVERIFY(!isolated.value("dark").isValid()); }
        { SettingsStore migrated; QCOMPARE(QDir::cleanPath(migrated.directory()),target);
          QCOMPARE(migrated.value("dark").toBool(),false);
          QCOMPARE(migrated.value("connection").toMap().value("baud").toInt(),9600);
          QCOMPARE(migrated.value("terminal/adbPath").toString(),QStringLiteral("工具/adb.exe"));
          QCOMPARE(migrated.value("updates/ignoredTag").toString(),QString("v0.4.2"));
          QVERIFY(!migrated.value("unrelated").isValid());QVERIFY(migrated.flush()); }
        QCOMPARE(readFile(legacy.fileName()),original);
        legacy.setValue("dark",true);legacy.sync();
        SettingsStore again;QCOMPARE(again.value("dark").toBool(),false);
    }
    void commandBatchNotifiesOnce()
    {
        QTemporaryDir dir(temporaryTemplate()); SettingsStore store(dir.path()); QuickCommands commands(&store);
        QSignalSpy changed(&commands,&QuickCommands::changed);
        for(int i=0;i<100;++i)QVERIFY(!commands.put("network",{{"title",QString::number(i)},{"text","x"}}).isEmpty());
        QCOMPARE(changed.size(),0);
        QTRY_COMPARE(changed.size(),1);
        QCOMPARE(commands.entries("network").size(),100);
    }
};
QTEST_GUILESS_MAIN(SettingsTests)
#include "settings_tests.moc"
