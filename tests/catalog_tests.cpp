#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include "commandcatalog.h"

class CatalogTests : public QObject {
    Q_OBJECT
    QByteArray package(const QString &version="1.0.0",const QString &title=QStringLiteral("列目录")) {
        return QJsonDocument(QJsonObject{{"schemaVersion",1},{"catalogVersion",version},{"minAppVersion","1.3.0"},
            {"commands",QJsonArray{QJsonObject{{"id","linux-list"},{"system","linux"},{"category","文件"},
                {"title",title},{"syntax","ls [路径]"},{"example","ls /tmp"},{"description","列出文件"},
                {"interaction","shell"},{"parameters",QJsonArray{}},{"options",QJsonArray{}},{"advanced",QJsonArray{}}}}}}).toJson();
    }
    bool write(const QString &path,const QByteArray &bytes) {QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();}
private slots:
    void seedOverrideAndRollback() {
        QTemporaryDir dir;const auto seed=dir.filePath("seed.json"),root=dir.filePath("user");
        QVERIFY(write(seed,package()));CommandCatalog catalog("1.3.0",root,seed);
        QCOMPARE(catalog.version(),QString("1.0.0"));QCOMPARE(catalog.entries().size(),1);
        QSignalSpy changed(&catalog,&CommandCatalog::commandsChanged);
        QVERIFY(catalog.apply(package("1.1.0",QStringLiteral("新目录"))));QCOMPARE(changed.count(),1);
        QCOMPARE(catalog.version(),QString("1.1.0"));QVERIFY(catalog.hasPrevious());
        CommandCatalog reopened("1.3.0",root,seed);QCOMPARE(reopened.version(),QString("1.1.0"));
        QVERIFY(reopened.rollback());QCOMPARE(reopened.version(),QString("1.0.0"));
        QVERIFY(reopened.rollback());QCOMPARE(reopened.version(),QString("1.1.0"));
        QVERIFY(write(root+"/current.json","bad"));QVERIFY(reopened.reload());
        QCOMPARE(reopened.version(),QString("1.0.0"));QVERIFY(reopened.status().contains(QStringLiteral("回退")));
    }
    void invalidKeepsGoodLibrary() {
        QTemporaryDir dir;const auto seed=dir.filePath("seed.json");QVERIFY(write(seed,package()));
        CommandCatalog catalog("1.3.0",dir.filePath("user"),seed);
        QVERIFY(!catalog.apply("{}"));QCOMPARE(catalog.version(),QString("1.0.0"));
        auto object=QJsonDocument::fromJson(package("1.1.0")).object();
        object["minAppVersion"]="99.0.0";QVERIFY(!catalog.apply(QJsonDocument(object).toJson()));
        object=QJsonDocument::fromJson(package()).object();auto items=object["commands"].toArray();items.append(items.first());object["commands"]=items;
        QVERIFY(!catalog.apply(QJsonDocument(object).toJson()));
        object=QJsonDocument::fromJson(package()).object();items=object["commands"].toArray();auto entry=items[0].toObject();entry["example"]="ls\nreboot";items[0]=entry;object["commands"]=items;
        QVERIFY(!catalog.apply(QJsonDocument(object).toJson()));QCOMPARE(catalog.entries().size(),1);
        QVERIFY(!QFileInfo::exists(dir.filePath("user/current.json")));
    }
    void importExportAndLegacyArray() {
        QTemporaryDir dir;CommandCatalog catalog("1.3.0",dir.filePath("user"),dir.filePath("missing"));
        QVERIFY(catalog.entries().isEmpty());QVERIFY(!catalog.status().isEmpty());
        const auto legacy=QJsonDocument(QJsonDocument::fromJson(package()).object()["commands"].toArray()).toJson();
        QVERIFY(write(dir.filePath("legacy.json"),legacy));
        QVERIFY(catalog.importFile(QUrl::fromLocalFile(dir.filePath("legacy.json"))));
        QCOMPARE(catalog.version(),QString("0.0.0"));
        const auto exportUrl=QUrl::fromLocalFile(dir.filePath("export.json"));QVERIFY(catalog.exportFile(exportUrl));
        CommandCatalog other("1.3.0",dir.filePath("other"),dir.filePath("missing"));
        QVERIFY(other.importFile(exportUrl));QCOMPARE(other.entries(),catalog.entries());
        QVERIFY(!catalog.exportFile(QUrl("https://example.com/file.json")));
    }
    void shippedExternalLibrary() {
        CommandCatalog catalog("1.3.0",QDir::tempPath()+"/serialstudio-catalog-test-no-user",QCoreApplication::applicationDirPath()+"/catalog/terminal_commands.json");
        QVERIFY2(catalog.entries().size()>=589,qPrintable(catalog.status()));
        QVERIFY(!QFile::exists(":/terminal/data/terminal_commands.json"));
    }
    void utf8NestedFieldsAndProtectedExports() {
        QTemporaryDir dir;const auto seed=dir.filePath("seed.json");QVERIFY(write(seed,package()));
        CommandCatalog catalog("1.3.0",dir.filePath("user"),seed);
        auto invalid=package();invalid.replace(QStringLiteral("列目录").toUtf8(),QByteArray(1,char(0xff)));
        QVERIFY(!catalog.apply(invalid));
        auto object=QJsonDocument::fromJson(package()).object();auto entries=object["commands"].toArray();
        auto entry=entries[0].toObject();entry["usage"]=QJsonObject{};entries[0]=entry;object["commands"]=entries;
        QVERIFY(!catalog.apply(QJsonDocument(object).toJson()));
        QVERIFY(catalog.apply(package("1.1.0")));
        QVERIFY(!catalog.exportFile(QUrl::fromLocalFile(dir.filePath("user/current.json"))));
#ifdef Q_OS_WIN
        QVERIFY(!catalog.exportFile(QUrl::fromLocalFile(dir.filePath("USER/CURRENT.JSON"))));
#endif
        QCOMPARE(catalog.version(),QString("1.1.0"));
    }
};
QTEST_GUILESS_MAIN(CatalogTests)
#include "catalog_tests.moc"
