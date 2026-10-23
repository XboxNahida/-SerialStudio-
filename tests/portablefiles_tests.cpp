#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <future>
#include "portablefiles.h"

class PortableFilesTests : public QObject {
    Q_OBJECT
    static bool write(const QString &path, const QByteArray &data)
    {
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
    }
    static QByteArray read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
private slots:
    void explicitDeleteAndCancellation()
    {
        QTemporaryDir dir(QCoreApplication::applicationDirPath()+"/delete-shell-XXXXXX");QVERIFY(dir.isValid());
        QVERIFY(write(dir.filePath(QStringLiteral("中文 文件.bin")),"keep"));
        std::atomic_bool cancelled{true};
        QVERIFY(!PortableFiles::remove(dir.filePath(QStringLiteral("中文 文件.bin")),&cancelled).isEmpty());
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("中文 文件.bin"))));
        cancelled.store(false);
        auto error=PortableFiles::remove(dir.filePath(QStringLiteral("中文 文件.bin")),&cancelled);
        QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("中文 文件.bin"))));
        QVERIFY(QDir().mkpath(dir.filePath("folder/sub")));QVERIFY(write(dir.filePath("folder/sub/data"),"payload"));
        error=PortableFiles::remove(dir.filePath("folder"),&cancelled);
        QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(!QFileInfo::exists(dir.filePath("folder")));
        QVERIFY(!PortableFiles::remove("shell-pidl:AAAAAA==").isEmpty());
        QVERIFY(!PortableFiles::remove("relative").isEmpty());
    }
    void shellListsChineseFilesAndStableIds()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("目录"))));
        QVERIFY(write(dir.filePath(QStringLiteral("中文 空格.bin")), QByteArray("hello\0world", 11)));
        auto result = std::async(std::launch::async, [&] { return PortableFiles::list(dir.path()); }).get();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.entries.size(), 2);
        bool fileFound = false, folderFound = false;
        for (const auto &value : result.entries) {
            const auto entry = value.toMap();
            QVERIFY(!entry.value("id").toString().isEmpty());
            if (entry.value("directory").toBool()) {
                folderFound = true;
                QCOMPARE(entry.value("name").toString(), QStringLiteral("目录"));
                auto sub = PortableFiles::list(entry.value("id").toString());
                QVERIFY2(sub.error.isEmpty(), qPrintable(sub.error));
                QVERIFY(sub.entries.isEmpty());
            } else {
                fileFound = true;
                QCOMPARE(entry.value("name").toString(), QStringLiteral("中文 空格.bin"));
                QCOMPARE(entry.value("size").toLongLong(), 11);
            }
        }
        QVERIFY(fileFound && folderFound);
    }
    void copyUsingReturnedShellIdAndRejectOverwrite()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir().mkpath(dir.filePath("out")));
        const QByteArray contents(700001, 'x');
        QVERIFY(write(dir.filePath(QStringLiteral("源文件.bin")), contents));
        const auto sourceListing = PortableFiles::list(dir.path());
        QString sourceId;
        for (const auto &value : sourceListing.entries) {
            const auto entry = value.toMap();
            if (!entry.value("directory").toBool()) sourceId = entry.value("id").toString();
        }
        QVERIFY2(!sourceId.isEmpty(), qPrintable(sourceListing.error));
        QList<int> updates;
        const auto result = std::async(std::launch::async, [&] {
            return PortableFiles::copy(sourceId, dir.filePath("out"), nullptr,
                [&](int value) { updates.append(value); });
        }).get();
        QVERIFY2(result.isEmpty(), qPrintable(result));
        QCOMPARE(read(dir.filePath(QStringLiteral("out/源文件.bin"))), contents);
        QVERIFY(!updates.isEmpty());
        QCOMPARE(updates.last(), 100);
        QVERIFY(write(dir.filePath(QStringLiteral("out/源文件.bin")), "keep"));
        const auto again = PortableFiles::copy(sourceId, dir.filePath("out"));
        QVERIFY(!again.isEmpty());
        QCOMPARE(read(dir.filePath(QStringLiteral("out/源文件.bin"))), QByteArray("keep"));
        QCOMPARE(QDir(dir.filePath("out")).entryList(QDir::Files).size(), 1);
    }
    void errorsAndPreCancelledDoNotWrite()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(write(dir.filePath("file"), "original"));
        QVERIFY(QDir().mkpath(dir.filePath("out")));
        QVERIFY(!PortableFiles::list("").error.isEmpty());
        QVERIFY(!PortableFiles::list("relative/path").error.isEmpty());
        for (const auto &id : QStringList{"shell-pidl:!", "shell-pidl:AQAA", "shell-pidl:BAABAA==",
                                         "shell-pidl:AAAAAA==", "shell-pidl:////AA=="})
            QVERIFY2(!PortableFiles::list(id).error.isEmpty(), qPrintable(id));
        QVERIFY(!PortableFiles::list(dir.filePath("missing")).error.isEmpty());
        QVERIFY(!PortableFiles::list(dir.filePath("file")).error.isEmpty());
        QVERIFY(!PortableFiles::copy(dir.path(), dir.filePath("out")).isEmpty());
        QVERIFY(!PortableFiles::copy(dir.filePath("file"), dir.filePath("file")).isEmpty());
        QVERIFY(!PortableFiles::copy(dir.filePath("missing"), dir.filePath("out")).isEmpty());
        std::atomic_bool cancelled{true};
        const auto error = PortableFiles::copy(dir.filePath("file"), dir.filePath("out"), &cancelled);
        QVERIFY(error.contains(QStringLiteral("取消")));
        QVERIFY(QDir(dir.filePath("out")).entryList(QDir::Files).isEmpty());
        QCOMPARE(read(dir.filePath("file")), QByteArray("original"));
    }
    void cancelFromWorkerProgressAndCollisionDuringStart()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(write(dir.filePath("file"), QByteArray(1000000, 'x')));
        QVERIFY(QDir().mkpath(dir.filePath("out")));
        std::atomic_bool cancelled{false};
        auto error = PortableFiles::copy(dir.filePath("file"), dir.filePath("out"), &cancelled,
            [&](int value) { if (value == 0) cancelled.store(true); });
        QVERIFY(error.contains(QStringLiteral("取消")));
        QVERIFY(QDir(dir.filePath("out")).entryList(QDir::Files).isEmpty());
        bool inserted = false;
        error = PortableFiles::copy(dir.filePath("file"), dir.filePath("out"), nullptr,
            [&](int value) {
                if (!inserted && value == 0) inserted = write(dir.filePath("out/file"), "keep race");
            });
        QVERIFY(inserted);
        QVERIFY(!error.isEmpty());
        QCOMPARE(read(dir.filePath("out/file")), QByteArray("keep race"));
        QCOMPARE(QDir(dir.filePath("out")).entryList(QDir::Files).size(), 1);
    }
};
// 显式指定设备才进行只读硬件回归；常规测试不要求连接实机。
static int inspectDevice(const QString &name)
{
    const auto roots = PortableFiles::roots();
    bool found = false, passed = roots.error.isEmpty();
    QJsonObject report{{"error", roots.error}, {"device", name}};
    QJsonArray storages;
    for (const auto &value : roots.entries) {
        const auto device = value.toMap();
        if (device.value("name").toString() != name) continue;
        found = true;
        const auto listed = PortableFiles::list(device.value("id").toString());
        report["deviceError"] = listed.error;
        passed = passed && listed.error.isEmpty() && !listed.entries.isEmpty();
        for (const auto &v : listed.entries) {
            const auto storage = v.toMap();
            if (!storage.value("directory").toBool()) continue;
            const auto contents = PortableFiles::list(storage.value("id").toString());
            passed = passed && contents.error.isEmpty();
            QJsonObject result{{"name", storage.value("name").toString()},
                {"error", contents.error}, {"entryCount", int(contents.entries.size())}};
            // 再进入一个子目录，验证多级对象ID能在独立COM调用之间恢复。
            for (const auto &c : contents.entries) {
                const auto child = c.toMap();
                if (!child.value("directory").toBool()) continue;
                const auto sub = PortableFiles::list(child.value("id").toString());
                result["childError"] = sub.error;
                result["childEntryCount"] = int(sub.entries.size());
                passed = passed && sub.error.isEmpty();
                break;
            }
            storages.append(result);
        }
    }
    report["found"] = found;
    report["storages"] = storages;
    report["passed"] = found && passed && !storages.isEmpty();
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly)) return 2;
    output.write(QJsonDocument(report).toJson());
    return report.value("passed").toBool() ? 0 : 1;
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 3 && args[1] == "--mtp-device") return inspectDevice(args[2]);
    PortableFilesTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "portablefiles_tests.moc"
