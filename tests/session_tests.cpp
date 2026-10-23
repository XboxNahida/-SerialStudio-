#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include "session.h"

class SessionTests : public QObject {
    Q_OBJECT
private slots:
    void disconnectedGuards()
    {
        Session s(nullptr, false);
        QVERIFY(!s.connected()); QVERIFY(!s.periodic());
        QVERIFY(!s.send("00", true, 0));
        QVERIFY(!s.lastError().isEmpty());
        QVERIFY(!s.startPeriodic("00", true, 0, 100));
        QVERIFY(!s.openPort({{"port","COM1"},{"baud",0}}));
        QVERIFY(!s.busy());
        QVERIFY(!s.configureParser(3, "GG", "0D0A"));
        QVERIFY(!s.configureParser(3, "AA", ""));
    }
    void boundedModel()
    {
        EventModel model(2);
        model.append({{"1","RX","a","",{},true},{"2","RX","b","",{},true},{"3","RX","c","",{},true}});
        QCOMPARE(model.rowCount(), 2); QCOMPARE(model.dropped(), qulonglong(1));
        QCOMPARE(model.data(model.index(0), EventModel::TitleRole).toString(), QString("b"));
        model.clear(); QCOMPARE(model.rowCount(), 0);
    }
    void demoIsLabelled()
    {
        Session s(nullptr, false);
        s.demo(); QTest::qWait(150);
        QVERIFY(s.terminal()->rowCount() > 0);
        QVERIFY(s.frames()->rowCount() > 0);
        QCOMPARE(s.rxBytes(), qulonglong(0));
        QCOMPARE(s.terminal()->entries().last().kind, QStringLiteral("DEMO"));
        QVERIFY(!s.connected());
        s.clearTerminal(); QCOMPARE(s.terminal()->rowCount(), 0);
        s.clearFrames(); QCOMPARE(s.frames()->rowCount(), 0);
    }
    void logExport()
    {
        Session s(nullptr, false);
        QTemporaryDir directory(QCoreApplication::applicationDirPath() + "/log-test-XXXXXX");
        QVERIFY(directory.isValid());
        s.demo();
        const QString path = directory.path() + "/events.jsonl";
        QVERIFY(s.saveLog(QUrl::fromLocalFile(path)));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        auto obj = QJsonDocument::fromJson(file.readLine()).object();
        QCOMPARE(obj.value("source").toString(), QStringLiteral("DEMO"));
        QVERIFY(obj.value("title").toString().contains(QStringLiteral("离线演示")));
        QVERIFY(!obj.value("hex").toString().isEmpty());
        QVERIFY(!s.saveLog(QUrl("https://example.invalid/log")));
    }
    void serverDisconnectCancelsCapture()
    {
        QTcpServer reservation;QVERIFY(reservation.listen(QHostAddress::LocalHost));const auto port=reservation.serverPort();reservation.close();
        Session s(nullptr,false);QTemporaryDir dir;QVERIFY(dir.isValid());
        QVERIFY(s.openNetwork({{"mode",1},{"host","127.0.0.1"},{"port",port}}));QTRY_VERIFY(s.connected());
        QTcpSocket peer;peer.connectToHost(QHostAddress::LocalHost,port);QTRY_VERIFY(s.writable());
        const auto output=dir.filePath("capture.bin");QVERIFY(s.analysis()->beginCapture(QUrl::fromLocalFile(output)));
        peer.write("first\n");QTRY_VERIFY(s.analysis()->captured()>0);
        peer.disconnectFromHost();QTRY_VERIFY(!s.writable());
        QVERIFY(!s.analysis()->capturing());QVERIFY(!QFile::exists(output));
        s.closePort();QTRY_VERIFY(!s.connected());
    }
};
QTEST_GUILESS_MAIN(SessionTests)
#include "session_tests.moc"
