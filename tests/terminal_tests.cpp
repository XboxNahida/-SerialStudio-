#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QSettings>
#include "terminalcontroller.h"
#include "toolpaths.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class TerminalTests : public QObject {
    Q_OBJECT
private slots:
    void transportSelection()
    {
        QString program,error;QStringList arguments;
        QVERIFY(TerminalController::launchSpec(1,{{"transport","ssh"},{"host","192.0.2.10"},{"user","tester"}},&program,&arguments,&error));
        QVERIFY(program.endsWith("ssh.exe",Qt::CaseInsensitive));
        QVERIFY(arguments.contains("powershell.exe"));QVERIFY(arguments.contains("tester@192.0.2.10"));
        QVERIFY(TerminalController::launchSpec(2,{{"transport","ssh"},{"host","192.0.2.10"},{"user","tester"}},&program,&arguments,&error));
        QVERIFY(program.endsWith("ssh.exe",Qt::CaseInsensitive));QVERIFY(arguments.contains("cmd.exe"));
        QVERIFY(TerminalController::launchSpec(0,{{"transport","adb"},{"adb",QCoreApplication::applicationFilePath()},{"serial","192.0.2.10:5555"}},&program,&arguments,&error));
        QCOMPARE(arguments,QStringList({"-s","192.0.2.10:5555","shell"}));
        QVERIFY(!TerminalController::launchSpec(2,{{"transport","unknown"}},&program,&arguments,&error));
        QVERIFY(!TerminalController::launchSpec(0,{{"transport","local"}},&program,&arguments,&error));
    }
    void serialGuards()
    {
        TerminalController terminal;
        QVERIFY(!terminal.open(0,{{"transport","serial"},{"presentation","qt"},{"port",""},{"baud",115200}}));
        QVERIFY(terminal.status().contains(QStringLiteral("串口")));
        QVERIFY(!terminal.open(0,{{"transport","serial"},{"presentation","qt"},{"port","NOT-A-REAL-PORT"},{"baud",0}}));
        QVERIFY(terminal.status().contains(QStringLiteral("波特率")));
        for(const auto &key:QStringList{"dataBits","parity","stopBits","flow"}){
            QVariantMap config{{"transport","serial"},{"presentation","qt"},{"port","NOT-A-REAL-PORT"},{"baud",115200}};
            config.insert(key,"garbage");
            QVERIFY(!terminal.open(0,config));
            QVERIFY2(terminal.status().contains(QStringLiteral("无效")),qPrintable(terminal.status()));
        }
        QVERIFY(!terminal.active());
    }
    void embeddedSession()
    {
        TerminalController terminal;
        QVERIFY(terminal.open(2,{{"presentation","qt"}}));
        QVERIFY(terminal.property("embedded").toBool());
        QCOMPARE(terminal.property("connectionSummary").toString(),QStringLiteral("本机 · Windows CMD"));
        QCOMPARE(terminal.property("transport").toString(),QStringLiteral("local"));
        auto *screen=terminal.property("screen").value<QObject*>();QVERIFY(screen);
        QTemporaryDir dir;const auto marker=dir.filePath("explicit.txt");
        QTest::qWait(400);
        QVERIFY(terminal.fill("echo explicit>\""+QDir::toNativeSeparators(marker)+"\""));
        QTest::qWait(150);QVERIFY(!QFileInfo::exists(marker));
        QVERIFY(terminal.sendKey("enter"));QTRY_VERIFY(QFileInfo::exists(marker));
        QVERIFY(terminal.sendKey("ctrlC"));
        QVERIFY(QMetaObject::invokeMethod(&terminal,"close"));
        QVERIFY(!terminal.active());QVERIFY(!terminal.fill("echo rejected"));
        QVERIFY(terminal.property("connectionSummary").toString().isEmpty());
    }
    void adbSdkFallback()
    {
        QTemporaryDir sdk;QVERIFY(sdk.isValid());QVERIFY(QDir().mkpath(sdk.filePath("platform-tools")));
        // 此用例验证没有受管工具时的 SDK 回退，不读取用户已安装的工具。
        ToolPaths::configure(sdk.filePath("managed"),{});
        struct ResetTools {~ResetTools(){ToolPaths::configure("",{});}} resetTools;
        const auto adb=sdk.filePath("platform-tools/adb.exe");QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),adb));
        const auto oldPath=qgetenv("PATH"),oldSdk=qgetenv("ANDROID_SDK_ROOT");
        qputenv("PATH",QByteArray());qputenv("ANDROID_SDK_ROOT",sdk.path().toUtf8());
        QString program,error;QStringList arguments;
        const bool found=TerminalController::launchSpec(3,{},&program,&arguments,&error);
        qputenv("PATH",oldPath);qputenv("ANDROID_SDK_ROOT",oldSdk);
        QVERIFY2(found,qPrintable(error));QCOMPARE(QFileInfo(program).canonicalFilePath(),QFileInfo(adb).canonicalFilePath());
    }
    void adbScanAndSelection()
    {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("devices.txt");
        auto output=[&](const QByteArray &text){QFile f(path);if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;return f.write(text)==text.size();};
        QVERIFY(output("List of devices attached\r\nboardA\tdevice product:rk model:RK3576 device:rk transport_id:1\r\n"));
        qputenv("SERIALSTUDIO_ADB_PROBE",path.toUtf8());
        struct Cleanup {~Cleanup(){qunsetenv("SERIALSTUDIO_ADB_PROBE");qunsetenv("SERIALSTUDIO_ADB_DELAY");}} cleanup;
        TerminalController terminal;auto *adb=terminal.property("adb").value<QObject*>();
        QVERIFY2(adb,"Android 终端缺少 ADB 设备枚举对象");
        QVERIFY(adb->setProperty("executablePath",QCoreApplication::applicationFilePath()));
        QVERIFY(QMetaObject::invokeMethod(adb,"refresh"));
        QTRY_VERIFY_WITH_TIMEOUT(!adb->property("scanning").toBool(),5000);
        QCOMPARE(adb->property("devices").toList().size(),1);
        QCOMPARE(adb->property("selectedSerial").toString(),QString("boardA"));
        QCOMPARE(adb->property("devices").toList().first().toMap().value("model").toString(),QString("RK3576"));
        QVERIFY(output("List of devices attached\nboardA unauthorized\nboardB offline\n"));
        QVERIFY(QMetaObject::invokeMethod(adb,"refresh"));QTRY_VERIFY(!adb->property("scanning").toBool());
        QCOMPARE(adb->property("devices").toList().size(),2);QVERIFY(!adb->property("ready").toBool());
        QVERIFY(adb->property("status").toString().contains("授权"));
        QVERIFY(output("List of devices attached\nboardA device\nboardC device\n"));
        QVERIFY(QMetaObject::invokeMethod(adb,"refresh"));QTRY_VERIFY(!adb->property("scanning").toBool());
        QVERIFY(!adb->property("ready").toBool());
        QVERIFY(adb->setProperty("selectedSerial","boardC"));QVERIFY(adb->property("ready").toBool());
        QVERIFY(QMetaObject::invokeMethod(adb,"refresh"));QTRY_VERIFY(!adb->property("scanning").toBool());
        QCOMPARE(adb->property("selectedSerial").toString(),QString("boardC"));
        // 页面启用期间应自动发现断开，不需要手动刷新。
        QVERIFY(adb->setProperty("enabled",true));QTRY_VERIFY(!adb->property("scanning").toBool());
        QVERIFY(output("List of devices attached\n"));
        QTRY_VERIFY_WITH_TIMEOUT(adb->property("devices").toList().isEmpty(),5000);QVERIFY(!adb->property("ready").toBool());
        QVERIFY(adb->setProperty("enabled",false));
        qputenv("SERIALSTUDIO_ADB_DELAY","9000");
        QVERIFY(QMetaObject::invokeMethod(adb,"refresh"));
        QTRY_VERIFY_WITH_TIMEOUT(!adb->property("scanning").toBool(),8000);
        QVERIFY(adb->property("status").toString().contains("超时"));
    }
    void launchArguments()
    {
        QString program,error;QStringList arguments;
        QVERIFY(TerminalController::launchSpec(0,{{"host","192.0.2.10"},{"user","root"},{"port",2222}},&program,&arguments,&error));
        QVERIFY(program.endsWith("ssh.exe",Qt::CaseInsensitive));QVERIFY(arguments.contains("-t"));
        QCOMPARE(arguments.last(),QString("root@192.0.2.10"));QVERIFY(arguments.contains("2222"));
        QVERIFY(!TerminalController::launchSpec(0,{{"host","-oBad"},{"user","root"}},&program,&arguments,&error));
        QVERIFY(TerminalController::launchSpec(1,{},&program,&arguments,&error));QVERIFY(program.endsWith("powershell.exe",Qt::CaseInsensitive));
        QVERIFY(TerminalController::launchSpec(2,{},&program,&arguments,&error));QVERIFY(program.endsWith("cmd.exe",Qt::CaseInsensitive));
        // 使用现有测试 EXE 代替 adb 路径，只验证构造，不执行。
        QVERIFY(TerminalController::launchSpec(3,{{"adb",QCoreApplication::applicationFilePath()},{"serial","192.0.2.1:5555"}},&program,&arguments,&error));
        QCOMPARE(arguments,QStringList({"-s","192.0.2.1:5555","shell"}));
        QVERIFY(!TerminalController::launchSpec(3,{{"adb",QCoreApplication::applicationFilePath()},{"serial","-s other"}},&program,&arguments,&error));
    }
    void catalogAndInputGuards()
    {
        TerminalController terminal;
        for(int mode=0;mode<4;++mode){const auto entries=terminal.commands(mode);QVERIFY(entries.size()>=15);for(const auto &entry:entries){const auto map=entry.toMap();QVERIFY(!map.value("syntax").toString().isEmpty());QVERIFY(TerminalController::validText(map.value("example").toString()));}}
        QVERIFY(!terminal.commands(0,"网").isEmpty());QVERIFY(terminal.commands(0,"不存在的指令xyz").isEmpty());
        QVERIFY(!terminal.commands(0,QStringLiteral("当前用户服务")).isEmpty());
        int dictionarySize=0;
        for(int mode=0;mode<4;++mode) {
            const auto entries=terminal.commands(mode);dictionarySize+=entries.size();
            for(const auto &entry:entries) {
                const auto map=entry.toMap();
                if(map.value("interaction")=="shell") {
                    QVERIFY(!map.value("location").toString().isEmpty());
                    QVERIFY(!map.value("usage").toString().isEmpty());
                    QVERIFY(map.contains("parameters"));QVERIFY(map.contains("advanced"));
                }
            }
        }
        QVERIFY(dictionarySize>=500);
        QVERIFY(TerminalController::validText("ls -al /tmp/测试"));
        QVERIFY(!TerminalController::validText("ls\nreboot"));QVERIFY(!TerminalController::validText("ls\r"));
        QVERIFY(!TerminalController::validText(QString(QChar(3))));QVERIFY(!terminal.fill("pwd"));QVERIFY(!terminal.sendKey("enter"));
    }
    void realConsoleFillDoesNotExecute_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("CMD")<<2;QTest::newRow("PowerShell")<<1;
    }
    void rawConsoleKeys()
    {
#ifdef Q_OS_WIN
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("raw.bin");
        qputenv("SERIALSTUDIO_CONSOLE_PROBE",path.toUtf8());TerminalController terminal;
        const bool opened=terminal.open(3,{{"adb",QCoreApplication::applicationFilePath()},{"hidden",true}});
        qunsetenv("SERIALSTUDIO_CONSOLE_PROBE");QVERIFY(opened);
        const auto handle=OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,DWORD(terminal.processId()));QVERIFY(handle);
        struct Cleanup {HANDLE h;~Cleanup(){if(WaitForSingleObject(h,0)==WAIT_TIMEOUT)TerminateProcess(h,0);CloseHandle(h);}} cleanup{handle};
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path+".ready"),5000);
        QVERIFY(terminal.fill("probe"));QTRY_VERIFY(!terminal.inputBusy());
        for(const auto &key:QStringList{"tab","up","ctrlC","ctrlD"}){
            QVERIFY(terminal.sendKey(key));QTRY_VERIFY(!terminal.inputBusy());
        }
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path),5000);
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));const auto bytes=file.readAll();
        QVERIFY2(bytes.startsWith("probe\t"),bytes.toHex().constData());QVERIFY(bytes.contains("\x1b[A"));QVERIFY(bytes.contains(char(3)));
#else
        QSKIP("Windows 原始控制台检查");
#endif
    }
    void realConsoleFillDoesNotExecute()
    {
#ifdef Q_OS_WIN
        QFETCH(int,mode);
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("结果.txt");
        TerminalController terminal;QVERIFY(terminal.open(mode,{{"hidden",true}}));
        const auto handle=OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,DWORD(terminal.processId()));QVERIFY(handle);
        struct Cleanup {HANDLE h;~Cleanup(){if(WaitForSingleObject(h,0)==WAIT_TIMEOUT)TerminateProcess(h,0);CloseHandle(h);}} cleanup{handle};
        QTest::qWait(400);
        const auto longCommand=mode==2?QStringLiteral("ping -t 127.0.0.1 >nul"):QStringLiteral("while ($true) { Start-Sleep -Milliseconds 100 }");
        QVERIFY(terminal.fill(longCommand));QTRY_VERIFY(!terminal.inputBusy());
        QVERIFY(terminal.sendKey("enter"));QTRY_VERIFY(!terminal.inputBusy());QTest::qWait(350);
        QVERIFY(terminal.sendKey("ctrlC"));QTRY_VERIFY(!terminal.inputBusy());QTest::qWait(350);
        QVERIFY(terminal.active());
        const auto command=mode==2?QStringLiteral("echo marker>\"%1\"").arg(QDir::toNativeSeparators(path)):
            QStringLiteral("[IO.File]::WriteAllText('%1','marker')").arg(path);
        QVERIFY(terminal.fill(command));
        QTRY_VERIFY_WITH_TIMEOUT(!terminal.inputBusy(),5000);
        QVERIFY2(!terminal.status().contains("失败"),qPrintable(terminal.status()));
        QTest::qWait(150);QVERIFY(!QFile::exists(path));
        QVERIFY(terminal.sendKey("enter"));QTRY_VERIFY_WITH_TIMEOUT(!terminal.inputBusy(),5000);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path),5000);QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(file.readAll().contains("marker"));file.close();
        QVERIFY(terminal.fill("exit"));QTRY_VERIFY(!terminal.inputBusy());QVERIFY(terminal.sendKey("enter"));
        QTRY_VERIFY_WITH_TIMEOUT(!terminal.active(),5000);QVERIFY(!terminal.fill("echo wrong"));
#else
        QSKIP("此处验证 Windows 控制台输入");
#endif
    }
};
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    app.setOrganizationName("SerialStudioTests");app.setApplicationName("TerminalTests");
    if(app.arguments().contains("devices")&&qEnvironmentVariableIsSet("SERIALSTUDIO_ADB_PROBE")){
        QThread::msleep(qEnvironmentVariableIntValue("SERIALSTUDIO_ADB_DELAY"));
        QFile file(QString::fromUtf8(qgetenv("SERIALSTUDIO_ADB_PROBE")));if(!file.open(QIODevice::ReadOnly))return 1;
        const auto bytes=file.readAll();fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);return 0;
    }
    if(app.arguments().contains("--adb-check")){
        TerminalController terminal;auto *adb=terminal.adb();adb->setExecutablePath("");
        QEventLoop loop;QObject::connect(adb,&AdbDiscovery::changed,&loop,[&]{if(!adb->scanning())loop.quit();});
        adb->refresh();if(adb->scanning())loop.exec();
        const auto bytes=QJsonDocument(QJsonObject{{"path",adb->resolvedPath()},{"selected",adb->selectedSerial()},
            {"status",adb->status()},{"devices",QJsonArray::fromVariantList(adb->devices())}}).toJson();
        fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);return adb->ready()?0:2;
    }
    if(app.arguments().contains("--terminal-input")){QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 1;return TerminalController::inputHelper(input.read(32769));}
#ifdef Q_OS_WIN
    if(app.arguments().last()=="shell"&&qEnvironmentVariableIsSet("SERIALSTUDIO_CONSOLE_PROBE")){
        const auto path=QString::fromUtf8(qgetenv("SERIALSTUDIO_CONSOLE_PROBE"));
        auto input=CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(input==INVALID_HANDLE_VALUE||!SetConsoleMode(input,ENABLE_VIRTUAL_TERMINAL_INPUT))return 1;
        SetConsoleCtrlHandler(nullptr,TRUE);
        QFile ready(path+".ready");if(!ready.open(QIODevice::WriteOnly))return 1;ready.close();
        QByteArray result;
        for(;;){wchar_t character=0;DWORD read=0;if(!ReadConsoleW(input,&character,1,&read,nullptr)||read!=1)return 1;
            if(character==4)break;result+=QString(QChar(character)).toUtf8();}
        CloseHandle(input);QFile output(path);if(!output.open(QIODevice::WriteOnly))return 1;
        return output.write(result)==result.size()?0:1;
    }
#endif
    TerminalTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "terminal_tests.moc"
