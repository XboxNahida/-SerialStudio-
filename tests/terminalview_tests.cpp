#include <QtTest>
#include <QClipboard>
#include <QGuiApplication>
#include <QFontMetricsF>
#include <QInputMethodEvent>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include "terminalview.h"
#include "terminalcontroller.h"

namespace {
QString visibleText(TerminalScreen *screen) {
    QStringList lines;
    for(int row=0;row<screen->rows();++row)lines.append(screen->lineText(row));
    return lines.join('\n');
}
QByteArray takeOutput(QSignalSpy &spy) {
    QByteArray result;
    while(!spy.isEmpty())result+=spy.takeFirst().front().toByteArray();
    return result;
}
struct ProcessEnvironment {
    QByteArray name,old;
    bool existed;
    ProcessEnvironment(const QByteArray &key,const QByteArray &value)
        :name(key),old(qgetenv(key.constData())),existed(qEnvironmentVariableIsSet(key.constData())) {
        qputenv(name.constData(),value);
    }
    ~ProcessEnvironment(){if(existed)qputenv(name.constData(),old);else qunsetenv(name.constData());}
};
void commitIme(QQuickWindow &window,const QString &text) {
    QInputMethodEvent event;event.setCommitString(text);QCoreApplication::sendEvent(&window,&event);
}
void typeAscii(QQuickWindow &window,const QByteArray &text) {
    for(const auto character:text)QTest::keyClick(&window,character);
}
struct ViewWindow {
    QQuickWindow window;
    TerminalView *view;
    explicit ViewWindow(TerminalScreen *screen):view(new TerminalView(window.contentItem())) {
        window.setGeometry(0,0,860,520);view->setSize(QSizeF(860,520));view->setScreen(screen);
        QObject::connect(&window,&QQuickWindow::widthChanged,view,[this](int width){view->setWidth(width);});
        QObject::connect(&window,&QQuickWindow::heightChanged,view,[this](int height){view->setHeight(height);});
        window.show();window.requestActivate();view->forceActiveFocus();
    }
};
}

class TerminalViewTests : public QObject {
    Q_OBJECT
private slots:
    void selectionSurvivesWheelAndNewOutput() {
        TerminalScreen screen;ViewWindow ui(&screen);
        for(int i=0;i<65;++i)screen.feed(QByteArray("line-")+QByteArray::number(i)+"\r\n");
        screen.scroll(8);QTest::qWait(50);
        QFont font("Consolas");font.setPixelSize(14);
        const auto cw=QFontMetricsF(font).horizontalAdvance('M');
        QTest::mousePress(&ui.window,Qt::LeftButton,Qt::NoModifier,QPoint(7,7));
        const QPoint end(qRound(6+7*cw),7);
        QMouseEvent move(QEvent::MouseMove,end,ui.window.mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(&ui.window,&move);
        QTest::mouseRelease(&ui.window,Qt::LeftButton,Qt::NoModifier,end);
        ui.view->copy();const auto original=QGuiApplication::clipboard()->text();
        QVERIFY(original.startsWith("line-"));
        QWheelEvent wheel(QPointF(20,20),ui.window.mapToGlobal(QPoint(20,20)),{},QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(ui.view,&wheel);
        ui.view->copy();QCOMPARE(QGuiApplication::clipboard()->text(),original);
        screen.feed("new-data\r\n");ui.view->copy();QCOMPARE(QGuiApplication::clipboard()->text(),original);
        screen.clearDisplay();QGuiApplication::clipboard()->setText("unchanged");ui.view->copy();
        QCOMPARE(QGuiApplication::clipboard()->text(),QString("unchanged"));
    }
    void keyboardImeClipboardAndResize() {
        TerminalScreen screen;ViewWindow ui(&screen);
        QTRY_VERIFY(ui.view->hasActiveFocus());
        QSignalSpy sent(&screen,&TerminalScreen::output);
        typeAscii(ui.window,"ab");
        QCOMPARE(takeOutput(sent),QByteArray("ab"));
        QTest::keyClick(&ui.window,Qt::Key_Up);QTest::keyClick(&ui.window,Qt::Key_Down);
        QTest::keyClick(&ui.window,Qt::Key_Left);QTest::keyClick(&ui.window,Qt::Key_Right);
        QTest::keyClick(&ui.window,Qt::Key_Tab);QTest::keyClick(&ui.window,Qt::Key_C,Qt::ControlModifier);
        QCOMPARE(takeOutput(sent),QByteArray("\x1b[A\x1b[B\x1b[D\x1b[C\t\x03"));
        commitIme(ui.window,QStringLiteral("中文输入"));
        QCOMPARE(takeOutput(sent),QStringLiteral("中文输入").toUtf8());

        screen.feed("\x1b[H\x1b[2Jcopy-text");
        QTest::qWait(100);
        QFont font("Consolas");font.setPixelSize(14);
        const auto cellWidth=QFontMetricsF(font).horizontalAdvance('M');
        QTest::mousePress(&ui.window,Qt::LeftButton,Qt::NoModifier,QPoint(7,7));
        const QPoint end(qRound(6+9*cellWidth),7);
        QMouseEvent move(QEvent::MouseMove,end,ui.window.mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(&ui.window,&move);
        QTest::mouseRelease(&ui.window,Qt::LeftButton,Qt::NoModifier,end);
        ui.view->copy();QCOMPARE(QGuiApplication::clipboard()->text(),QString("copy-text"));
        QGuiApplication::clipboard()->clear();
        QTest::keyClick(&ui.window,Qt::Key_C,Qt::ControlModifier|Qt::ShiftModifier);
        QCOMPARE(QGuiApplication::clipboard()->text(),QString("copy-text"));
        QVERIFY(takeOutput(sent).isEmpty());
        QGuiApplication::clipboard()->setText(QStringLiteral("粘贴文本"));
        QTest::keyClick(&ui.window,Qt::Key_V,Qt::ControlModifier|Qt::ShiftModifier);
        QCOMPARE(takeOutput(sent),QStringLiteral("粘贴文本").toUtf8());
        const auto before=QSize(screen.columns(),screen.rows());
        QSignalSpy resized(&screen,&TerminalScreen::sizeChanged);
        ui.window.resize(1100,660);
        QTRY_VERIFY(!resized.isEmpty());
        QVERIFY(screen.columns()>before.width());QVERIFY(screen.rows()>before.height());
    }
    void realShellViewInput() {
        QTemporaryDir files;QVERIFY(files.isValid());
        QFile completion(files.filePath("tabcheck_unique.txt"));QVERIFY(completion.open(QIODevice::WriteOnly));
        QCOMPARE(completion.write("__TAB_FILE_CONTENT__\r\n"),qint64(22));completion.close();
        TerminalController terminal;ViewWindow ui(terminal.screen());
        QVERIFY(terminal.open(2,{{"presentation","qt"}}));
        QTRY_VERIFY(ui.view->hasActiveFocus());
        typeAscii(ui.window,"echo __VIEW_%COMSPEC%__");
        QTest::keyClick(&ui.window,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(visibleText(terminal.screen()).toLower().contains("__view_c:\\windows\\system32\\cmd.exe__"),10000);
        typeAscii(ui.window,"echo ");commitIme(ui.window,QStringLiteral("中文回显"));
        QTest::keyClick(&ui.window,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(visibleText(terminal.screen()).contains(QStringLiteral("中文回显")),10000);
        QGuiApplication::clipboard()->setText("echo __CLIP_%COMSPEC%__");
        QTest::keyClick(&ui.window,Qt::Key_V,Qt::ControlModifier|Qt::ShiftModifier);
        QTest::keyClick(&ui.window,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(visibleText(terminal.screen()).toLower().contains("__clip_c:\\windows\\system32\\cmd.exe__"),10000);
        QTest::keyClick(&ui.window,Qt::Key_Up);QTest::keyClick(&ui.window,Qt::Key_C,Qt::ControlModifier);
        QVERIFY(terminal.fill("cd /d \""+QDir::toNativeSeparators(files.path())+"\""));
        QTest::keyClick(&ui.window,Qt::Key_Return);
        QTest::qWait(100);
        typeAscii(ui.window,"type tabcheck_");QTest::keyClick(&ui.window,Qt::Key_Tab);
        QTest::keyClick(&ui.window,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(visibleText(terminal.screen()).contains("__TAB_FILE_CONTENT__"),10000);
        QVERIFY(terminal.fill("ping -t 127.0.0.1 >nul"));QTest::keyClick(&ui.window,Qt::Key_Return);
        QTest::qWait(200);QTest::keyClick(&ui.window,Qt::Key_C,Qt::ControlModifier);
        typeAscii(ui.window,"echo __INTERRUPTED_%COMSPEC%__");QTest::keyClick(&ui.window,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(visibleText(terminal.screen()).toLower().contains("__interrupted_c:\\windows\\system32\\cmd.exe__"),10000);
        QVERIFY(terminal.active());ui.window.resize(1050,640);QTest::qWait(50);
        QVERIFY(terminal.active());terminal.close();QVERIFY(!terminal.active());
    }
    void realVimAssistantCommand() {
        const QString program="C:/Program Files/Git/usr/bin/vim.exe";
        if(!QFileInfo(program).isFile())QSKIP("本机未安装真实 vim");
        ProcessEnvironment term("TERM","xterm-256color");
        QTemporaryDir files;QVERIFY(files.isValid());const auto file=files.filePath("vim-assistant.txt");
        TerminalController terminal;ViewWindow ui(terminal.screen());
        QVERIFY(terminal.open(2,{{"presentation","qt"}}));
        const auto command='"'+QDir::toNativeSeparators(program)+"\" -u NONE -U NONE -i NONE -n -N --noplugin -T xterm-256color \""+file+'"';
        QVERIFY(terminal.fill(command));QTest::keyClick(&ui.window,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(terminal.screen()->alternate(),10000);
        QTest::keyClick(&ui.window,'i');commitIme(ui.window,QStringLiteral("助手保存验证"));
        QTRY_VERIFY_WITH_TIMEOUT(visibleText(terminal.screen()).contains(QStringLiteral("助手保存验证")),10000);
        QVERIFY(terminal.applyCommand({{"interaction","editor-key"},{"key",":wq"}}));
        QTest::qWait(100);QVERIFY(!QFileInfo::exists(file));
        QTest::keyClick(&ui.window,Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(file),10000);
        QTRY_VERIFY_WITH_TIMEOUT(!terminal.screen()->alternate(),10000);
        QFile saved(file);QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(saved.readAll()).trimmed(),QStringLiteral("助手保存验证"));
        QVERIFY(terminal.active());terminal.close();
    }
    void realEditors_data() {
        QTest::addColumn<QString>("editor");
        QTest::newRow("vim")<<QString("vim");QTest::newRow("nano")<<QString("nano");
    }
    void realEditors() {
        QFETCH(QString,editor);
        const QString program="C:/Program Files/Git/usr/bin/"+editor+".exe";
        if(!QFileInfo(program).isFile())QSKIP("本机未安装此真实编辑器");
        ProcessEnvironment term("TERM","xterm-256color");
        QTemporaryDir files;QVERIFY(files.isValid());const auto file=files.filePath(editor+"-临时验证.txt");
        TerminalScreen screen;screen.resize(100,30);PseudoTerminal pty;QByteArray raw;
        connect(&pty,&PseudoTerminal::received,&screen,[&](const QByteArray &bytes){raw+=bytes;screen.feed(bytes);});
        connect(&screen,&TerminalScreen::output,&pty,[&](const QByteArray &bytes){pty.write(bytes);});
        connect(&screen,&TerminalScreen::sizeChanged,&pty,&PseudoTerminal::resize);
        QSignalSpy finished(&pty,&PseudoTerminal::finished);
        const QStringList arguments=editor=="vim"?QStringList{"-u","NONE","-U","NONE","-i","NONE","-n","-N","--noplugin","-T","xterm-256color",file}
            :QStringList{"--ignorercfiles","--nowrap",file};
        QVERIFY2(pty.start(program,arguments,screen.columns(),screen.rows()),qPrintable(pty.error()));
        QTRY_VERIFY_WITH_TIMEOUT(screen.alternate(),10000);
        const auto content=QStringLiteral("editor-中文验证");
        if(editor=="vim")screen.key(Qt::Key_I,0,"i");
        screen.paste(content);
        QTRY_VERIFY_WITH_TIMEOUT(visibleText(&screen).contains(content),10000);
        screen.resize(110,34);
        if(editor=="vim") {
            screen.key(Qt::Key_Escape,0,{});
            for(const auto character:QString(":wq"))screen.key(0,0,QString(character));
            QTest::qWait(100);QVERIFY(!QFileInfo::exists(file));
            screen.key(Qt::Key_Return,0,{});
        } else {
            screen.key(Qt::Key_O,Qt::ControlModifier,{});
            QTest::qWait(100);QVERIFY(!QFileInfo::exists(file));
            screen.key(Qt::Key_Return,0,{});
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(file),10000);
            screen.key(Qt::Key_X,Qt::ControlModifier,{});
        }
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000);
        QCOMPARE(finished.front().front().toInt(),0);QVERIFY(!screen.alternate());
        QFile saved(file);QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto savedBytes=saved.readAll();QVERIFY2(savedBytes.contains(content.toUtf8()),savedBytes.toHex().constData());
        QVERIFY(!pty.running());
        qInfo().noquote()<<editor<<"saved; output bytes="<<raw.size();
    }
};
int main(int argc,char **argv) {
    QTemporaryDir fonts;
    QFile::copy("C:/Windows/Fonts/consola.ttf",fonts.filePath("consola.ttf"));
    QFile::copy("C:/Windows/Fonts/msyh.ttc",fonts.filePath("msyh.ttc"));
    ProcessEnvironment platform("QT_QPA_PLATFORM","offscreen"),backend("QSG_RHI_BACKEND","software");
    ProcessEnvironment fontDirectory("QT_QPA_FONTDIR",fonts.path().toUtf8());
    QGuiApplication app(argc,argv);QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    app.setOrganizationName("SerialStudioTests");app.setApplicationName("TerminalViewTests");
    TerminalViewTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "terminalview_tests.moc"
