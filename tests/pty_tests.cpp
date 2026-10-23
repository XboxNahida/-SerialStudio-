#include <QtTest>
#include "pseudoterminal.h"
#include <windows.h>

class PtyTests : public QObject {
    Q_OBJECT
private slots:
    void realShellAndClose() {
        PseudoTerminal terminal;QByteArray data;
        connect(&terminal,&PseudoTerminal::received,this,[&](const QByteArray &bytes){data+=bytes;});
        connect(&terminal,&PseudoTerminal::finished,this,[](int code){qInfo()<<"PTY exit"<<Qt::hex<<quint32(code);});
        const auto shell=qEnvironmentVariable("SystemRoot","C:/Windows")+"/System32/cmd.exe";
        QVERIFY(terminal.start(shell,{"/D","/Q"},80,24));
        QTRY_VERIFY_WITH_TIMEOUT(!data.isEmpty(),10000);
        QVERIFY2(terminal.write("echo __PTY_RESULT_%COMSPEC%__\r"),data.constData());
        QTRY_VERIFY_WITH_TIMEOUT(data.contains("__PTY_RESULT_C:\\Windows\\system32\\cmd.exe__")||data.contains("__PTY_RESULT_C:\\WINDOWS\\system32\\cmd.exe__"),10000);
        terminal.resize(90,30);QVERIFY(terminal.running());
        terminal.close();QVERIFY(!terminal.running());
        QVERIFY(!terminal.write("echo should-not-send\r"));
    }
    void invalidProgramAndRestart() {
        PseudoTerminal terminal;
        QVERIFY(!terminal.start("C:/does-not-exist/no.exe",{},80,24));
        QVERIFY(!terminal.running());QVERIFY(!terminal.error().isEmpty());
        QByteArray data;
        connect(&terminal,&PseudoTerminal::received,this,[&](const QByteArray &bytes){data+=bytes;});
        for(int i=0;i<3;++i){
            data.clear();
            QVERIFY(terminal.start(qEnvironmentVariable("SystemRoot")+"/System32/cmd.exe",{"/D","/Q"},80,24));
            QVERIFY(terminal.write("echo __RESTART_%COMSPEC%__\r"));
            QTRY_VERIFY_WITH_TIMEOUT(data.toLower().contains("__restart_c:\\windows\\system32\\cmd.exe__"),10000);
            terminal.close();
        }
    }
    void powershellAndFinalOutput() {
        PseudoTerminal terminal;QByteArray data;bool tailBeforeFinished=false;
        connect(&terminal,&PseudoTerminal::received,this,[&](const QByteArray &bytes){data+=bytes;});
        connect(&terminal,&PseudoTerminal::finished,this,[&](int){tailBeforeFinished=data.contains("__PS_TAIL__");});
        QSignalSpy finished(&terminal,&PseudoTerminal::finished);
        const auto shell=qEnvironmentVariable("SystemRoot")+"/System32/WindowsPowerShell/v1.0/powershell.exe";
        QVERIFY(terminal.start(shell,{"-NoLogo","-NoProfile"},100,30));
        QVERIFY(terminal.write("[Console]::WriteLine(('__PS_' + 'READY__'))\r"));
        QTRY_VERIFY_WITH_TIMEOUT(data.contains("__PS_READY__"),15000);
        QVERIFY(terminal.write("[Console]::WriteLine(('__PS_' + 'TAIL__')); exit 17\r"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,15000);
        QCOMPARE(finished.front().front().toInt(),17);
        QVERIFY2(tailBeforeFinished,data.constData());
    }
    void finalFrameBeforeFinished() {
        PseudoTerminal terminal;QByteArray data;bool tailBeforeFinished=false;
        connect(&terminal,&PseudoTerminal::received,this,[&](const QByteArray &bytes){data+=bytes;});
        connect(&terminal,&PseudoTerminal::finished,this,[&](int){tailBeforeFinished=data.contains("__TAIL_1023__");});
        QSignalSpy finished(&terminal,&PseudoTerminal::finished);
        QVERIFY(terminal.start(QCoreApplication::applicationFilePath(),{"--pty-tail-child"},120,40));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000);
        QCOMPARE(finished.front().front().toInt(),37);
        QVERIFY2(tailBeforeFinished,data.right(400).constData());
    }
    void backpressureDoesNotBlockCaller() {
        PseudoTerminal terminal;QSignalSpy received(&terminal,&PseudoTerminal::received);
        QVERIFY(terminal.start(QCoreApplication::applicationFilePath(),{"--pty-block-child"},80,24));
        QTRY_VERIFY_WITH_TIMEOUT(!received.isEmpty(),10000);
        QElapsedTimer elapsed;elapsed.start();
        bool rejected=false;
        const QByteArray block(65536,'x');
        for(int i=0;i<256;++i)if(!terminal.write(block)){rejected=true;break;}
        QVERIFY2(elapsed.elapsed()<500,"管道背压阻塞调用线程");
        QVERIFY(rejected);
        elapsed.restart();terminal.close();
        QVERIFY2(elapsed.elapsed()<2000,"关闭未能取消阻塞写入");
    }
    void closeDrainsBufferedOutput() {
        PseudoTerminal terminal;QByteArray data;
        connect(&terminal,&PseudoTerminal::received,this,[&](const QByteArray &bytes){data+=bytes;});
        QVERIFY(terminal.start(QCoreApplication::applicationFilePath(),{"--pty-tail-child"},120,40));
        Sleep(500);
        terminal.close();
        QVERIFY2(data.contains("__TAIL_1023__"),data.right(400).constData());
    }
};
int main(int argc,char **argv) {
    if(argc>1 && QByteArray(argv[1])=="--pty-tail-child") {
        for(int i=0;i<1024;++i){
            const auto line=QByteArray("__TAIL_")+QByteArray::number(i)+"__\r\n";
            DWORD written=0;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),line.constData(),DWORD(line.size()),&written,nullptr);
        }
        return 37;
    }
    if(argc>1 && QByteArray(argv[1])=="--pty-block-child") {
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),ENABLE_VIRTUAL_TERMINAL_INPUT);
        DWORD written=0;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"__BLOCK_READY__\r\n",17,&written,nullptr);
        Sleep(3000);return 0;
    }
    QCoreApplication app(argc,argv);PtyTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "pty_tests.moc"
