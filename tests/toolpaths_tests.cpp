#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "toolpaths.h"
class ToolPathsTests: public QObject {
    Q_OBJECT
    QString create(const QString &path) { QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);if(!f.open(QIODevice::WriteOnly))return {};f.write("test");return path; }
private slots:
    void emptyLaunchersAreSkippedAndSearchContinues() {
        QTemporaryDir d;
        const auto emptyPath=d.filePath("aliases/python.exe");
        QDir().mkpath(QFileInfo(emptyPath).absolutePath());
        QFile alias(emptyPath);QVERIFY(alias.open(QIODevice::WriteOnly));alias.close();
        const auto real=create(d.filePath("runtime/python.exe"));
        const auto find=[&](const QString &custom) {
            return ToolPaths::locate("python",custom,d.filePath("managed"),d.filePath("app"),
                {d.filePath("aliases"),d.filePath("runtime")});
        };
        QCOMPARE(find(""),real);
        QVERIFY(find(emptyPath).isEmpty());
        QCOMPARE(find(real),real);
    }
    void childEnvironmentUsesToolsWithoutChangingParent() {
        QTemporaryDir d;create(d.filePath("python/3.13.7/python.exe"));
        const auto oldPath=qEnvironmentVariable("PATH");
        ToolPaths::configure(d.path(),{});
        QVERIFY(ToolPaths::environment().value("PATH").startsWith(QDir::toNativeSeparators(d.filePath("python/3.13.7"))));
        QCOMPARE(qEnvironmentVariable("PATH"),oldPath);
        ToolPaths::configure("",{});
    }
    void precedenceAndInvalidOverride() {
        QTemporaryDir d;const auto user=create(d.filePath("user/ssh.exe"));
        const auto bundled=create(d.filePath("managed/openssh/10.4/OpenSSH-Win64/ssh.exe"));
        const auto system=create(d.filePath("path/ssh.exe"));
        const auto find=[&](const QString &overridePath) {return ToolPaths::locate("ssh",overridePath,d.filePath("managed"),d.filePath("app"),{d.filePath("path")});};
        QCOMPARE(find(user),user);QCOMPARE(find(""),bundled);
        QVERIFY(find(d.filePath("missing.exe")).isEmpty());
        QVERIFY(QFile::remove(bundled));QCOMPARE(find(""),system);
        QVERIFY(ToolPaths::locate("unknown","",d.path(),d.path(),{d.path()}).isEmpty());
    }
    void unknownJavaJlinkIsNotAutomaticallySelected() {
        QTemporaryDir d;create(d.filePath("jlink.exe"));
        QVERIFY(ToolPaths::locate("jlink","",d.path(),d.path(),{d.path()}).isEmpty());
    }
};
QTEST_GUILESS_MAIN(ToolPathsTests)
#include "toolpaths_tests.moc"
