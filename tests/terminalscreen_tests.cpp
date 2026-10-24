#include <QtTest>
#include "terminalscreen.h"

class ScreenTests : public QObject {
    Q_OBJECT
private slots:
    void clearDisplayPreservesModesAndSendsNothing() {
        TerminalScreen screen;screen.resize(20,4);
        for(int i=0;i<10;++i)screen.feed("old\r\n");
        QVERIFY(screen.historySize()>0);
        screen.feed("\x1b[?1049h\x1b[?2004hvisible");
        QSignalSpy sent(&screen,&TerminalScreen::output);
        QVERIFY2(QMetaObject::invokeMethod(&screen,"clearDisplay"),"尚未提供仅清理显示的操作");
        QCOMPARE(screen.historySize(),0);QVERIFY(screen.alternate());QVERIFY(sent.isEmpty());
        for(int row=0;row<screen.rows();++row)QVERIFY(screen.lineText(row).isEmpty());
        screen.paste("next");QVERIFY(!sent.isEmpty());
        QByteArray bytes;for(const auto &signal:sent)bytes+=signal.first().toByteArray();
        QVERIFY(bytes.contains("\x1b[200~"));QVERIFY(bytes.contains("next"));
    }
    void cursorAndErase() {
        TerminalScreen screen;screen.resize(20,4);
        screen.feed("hello\rXY\x1b[K");
        QCOMPARE(screen.lineText(0),QString("XY"));
        screen.feed("\x1b[2;3Hok");
        QCOMPARE(screen.lineText(1),QString("  ok"));
    }
    void unicodeFragments() {
        TerminalScreen screen;screen.resize(20,4);
        const auto bytes=QStringLiteral("中文🙂").toUtf8();
        for(const auto byte:bytes)screen.feed(QByteArray(1,byte));
        QCOMPARE(screen.lineText(0),QStringLiteral("中文🙂"));
    }
    void alternateScreenAndColor() {
        TerminalScreen screen;screen.resize(20,4);screen.feed("normal");
        screen.feed("\x1b[?1049h\x1b[H\x1b[31mred");
        QVERIFY(screen.alternate());QCOMPARE(screen.lineText(0),QString("red"));
        const auto cells=screen.cells();QVERIFY(!cells.isEmpty());
        QVERIFY(cells.first().toMap().value("foreground").value<QColor>().red()>100);
        screen.feed("\x1b[?1049l");
        QVERIFY(!screen.alternate());QCOMPARE(screen.lineText(0),QString("normal"));
    }
    void scrollbackAndKeys() {
        TerminalScreen screen;screen.resize(10,3);
        QSignalSpy sent(&screen,&TerminalScreen::output);
        screen.key(Qt::Key_Up,0,{});QVERIFY(!sent.isEmpty());
        QCOMPARE(sent.takeFirst().first().toByteArray(),QByteArray("\x1b[A"));
        screen.key(Qt::Key_C,Qt::ControlModifier,QString("c"));
        QCOMPARE(sent.takeFirst().first().toByteArray(),QByteArray(1,3));
        for(int i=0;i<10;++i)screen.feed(QByteArray::number(i)+"\r\n");
        QVERIFY(screen.historySize()>0);screen.scroll(2);QVERIFY(screen.scrollOffset()>0);
        QVERIFY(!screen.selection(0,0,1,9).isEmpty());
        screen.reset();QCOMPARE(screen.historySize(),0);
    }
};
QTEST_GUILESS_MAIN(ScreenTests)
#include "terminalscreen_tests.moc"
