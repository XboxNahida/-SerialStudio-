#include "terminalcontroller.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
const QStringList systems={"linux","powershell","cmd","android"};
const QStringList keys={"enter","tab","escape","up","down","left","right","backspace","ctrlC","ctrlD"};
QString executable(const QString &relative,const QString &fallback)
{
    const auto path=QDir(qEnvironmentVariable("SystemRoot","C:/Windows")).filePath(relative);
    return QFileInfo::exists(path)?path:QStandardPaths::findExecutable(fallback);
}
#ifdef Q_OS_WIN
quint64 creationTime(HANDLE handle)
{
    FILETIME created,exited,kernel,user;
    if(!GetProcessTimes(handle,&created,&exited,&kernel,&user))return 0;
    return (quint64(created.dwHighDateTime)<<32)|created.dwLowDateTime;
}
QString quoteArgument(const QString &value)
{
    QString result="\"";int slashes=0;
    for(const auto c:value){
        if(c=='\\'){++slashes;continue;}
        if(c=='\"')result+=QString(2*slashes+1,'\\');else result+=QString(slashes,'\\');
        result+=c;slashes=0;
    }
    return result+QString(2*slashes,'\\')+'\"';
}
#endif
}

TerminalController::TerminalController(QObject *parent):QObject(parent)
{
    Q_INIT_RESOURCE(terminal_catalog);
    QFile catalog(":/terminal/data/terminal_commands.json");
    if(catalog.open(QIODevice::ReadOnly))m_commands=QJsonDocument::fromJson(catalog.readAll()).array().toVariantList();
    m_poll.setInterval(250);
    connect(&m_poll,&QTimer::timeout,this,&TerminalController::refresh);
    m_timeout.setSingleShot(true);m_timeout.setInterval(3000);
    connect(&m_timeout,&QTimer::timeout,this,[this]{
        if(inputBusy()){m_input.kill();m_status=QStringLiteral("输入投递失败：辅助进程超时");emit changed();}
    });
    connect(&m_input,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){
        m_timeout.stop();m_status=QStringLiteral("输入投递失败：%1").arg(m_input.errorString());emit changed();
    });
    connect(&m_input,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
        m_timeout.stop();
        m_status=code==0&&exit==QProcess::NormalExit?QStringLiteral("输入已送达；文字未自动回车，请在终端检查"):QStringLiteral("输入投递失败：目标终端已关闭或无法访问（%1，%2）").arg(code).arg(QString::fromUtf8(m_input.readAllStandardError()));
        emit changed();
    });
}
TerminalController::~TerminalController()
{
    if(inputBusy()){m_input.kill();m_input.waitForFinished(1000);}
#ifdef Q_OS_WIN
    if(m_handle)CloseHandle(static_cast<HANDLE>(m_handle));
#endif
}
bool TerminalController::active() const
{
#ifdef Q_OS_WIN
    return m_handle&&WaitForSingleObject(static_cast<HANDLE>(m_handle),0)==WAIT_TIMEOUT;
#else
    return false;
#endif
}
bool TerminalController::fail(const QString &message){m_status=message;emit changed();return false;}
void TerminalController::refresh()
{
    if(!m_handle||active())return;
#ifdef Q_OS_WIN
    CloseHandle(static_cast<HANDLE>(m_handle));
#endif
    m_handle=nullptr;m_pid=0;m_started=0;m_mode=-1;m_poll.stop();
    m_status=QStringLiteral("交互终端已关闭，请重新打开");emit changed();
}
bool TerminalController::launchSpec(int mode,const QVariantMap &config,QString *program,QStringList *arguments,QString *error)
{
    if(!program||!arguments||!error)return false;
    program->clear();arguments->clear();error->clear();
    if(mode==0){
        const auto host=config.value("host").toString().trimmed(),user=config.value("user","root").toString().trimmed();
        const auto port=config.value("port",22).toInt();const auto key=config.value("key").toString();
        if(!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,252}$").match(host).hasMatch()||
           !QRegularExpression("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}$").match(user).hasMatch()||port<1||port>65535){*error=QStringLiteral("SSH 主机、用户或端口格式无效");return false;}
        if(!key.isEmpty()&&!QFileInfo(key).isFile()){*error=QStringLiteral("SSH 私钥文件不存在");return false;}
        *program=executable("System32/OpenSSH/ssh.exe","ssh.exe");
        *arguments={"-p",QString::number(port),"-o","ConnectTimeout=8"};
        if(!key.isEmpty())*arguments<<"-i"<<key;
        *arguments<<"-t"<<"--"<<(user+"@"+host);
    }else if(mode==1){
        *program=executable("System32/WindowsPowerShell/v1.0/powershell.exe","powershell.exe");*arguments={"-NoLogo","-NoExit"};
    }else if(mode==2){
        *program=executable("System32/cmd.exe","cmd.exe");*arguments={"/D","/K"};
    }else if(mode==3){
        const auto adb=config.value("adb").toString().trimmed();
        *program=AdbDiscovery::resolveExecutable(adb);
        const auto serial=config.value("serial").toString().trimmed();
        if(!serial.isEmpty()&&!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,255}$").match(serial).hasMatch()){
            *error=QStringLiteral("ADB 设备序列号格式无效");return false;
        }
        if(!serial.isEmpty())*arguments<<"-s"<<serial;
        *arguments<<"shell";
    }else{*error=QStringLiteral("未知终端系统");return false;}
    if(program->isEmpty()||!QFileInfo(*program).isFile()){
        *error=mode==3?QStringLiteral("未找到 adb.exe，请安装 Android platform-tools 并填写完整路径"):QStringLiteral("未找到所需系统终端程序");return false;
    }
    *program=QFileInfo(*program).absoluteFilePath();return true;
}
bool TerminalController::open(int mode,const QVariantMap &config)
{
    refresh();
    if(active())return fail(QStringLiteral("已有受控终端，请先关闭该终端窗口再打开其他系统"));
    if(inputBusy())return fail(QStringLiteral("输入投递中，请稍候"));
    QString program,error;QStringList arguments;
    if(!launchSpec(mode,config,&program,&arguments,&error))return fail(error);
#ifdef Q_OS_WIN
    // 显式创建专用控制台；避免启动器继承无控制台测试环境中的标准句柄。
    QString command=quoteArgument(QDir::toNativeSeparators(program));
    for(const auto &argument:arguments)command+=' '+quoteArgument(argument);
    auto native=command.toStdWString();STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION info{};
    if(!CreateProcessW(reinterpret_cast<LPCWSTR>(program.utf16()),native.data(),nullptr,nullptr,FALSE,CREATE_NEW_CONSOLE,nullptr,nullptr,&startup,&info))
        return fail(QStringLiteral("无法打开交互终端：Windows 错误 %1").arg(GetLastError()));
    CloseHandle(info.hThread);
    m_handle=info.hProcess;m_started=creationTime(info.hProcess);m_pid=info.dwProcessId;m_mode=mode;m_poll.start();
    m_status=QStringLiteral("已打开 %1 终端；辅助输入只追加文字，不自动回车").arg(systems.at(mode));emit changed();return true;
#else
    return fail(QStringLiteral("定向终端输入当前仅在 Windows 上支持"));
#endif
}
bool TerminalController::validText(const QString &text)
{
    if(text.trimmed().isEmpty()||text.size()>4096)return false;
    for(const QChar c:text)if(c.category()==QChar::Other_Control||c.category()==QChar::Separator_Line||c.category()==QChar::Separator_Paragraph)return false;
    return true;
}
bool TerminalController::fill(const QString &text)
{
    if(!validText(text))return fail(QStringLiteral("请输入 1–4096 字符的单行指令，不允许换行或控制字符"));
    return dispatch("text",text);
}
bool TerminalController::sendKey(const QString &key)
{
    if(!keys.contains(key))return fail(QStringLiteral("不支持的终端按键"));
    return dispatch("key",key);
}
bool TerminalController::dispatch(const QString &kind,const QString &value)
{
    refresh();
    if(!active())return fail(QStringLiteral("请先打开对应的交互终端"));
    if(inputBusy())return fail(QStringLiteral("输入投递中，请稍候"));
    const auto request=QJsonDocument(QJsonObject{{"pid",double(m_pid)},{"started",QString::number(m_started)},{"kind",kind},{"value",value}}).toJson(QJsonDocument::Compact);
    m_input.start(QCoreApplication::applicationFilePath(),{"--terminal-input"});
    m_input.write(request);m_input.closeWriteChannel();m_timeout.start();
    m_status=QStringLiteral("正在投递输入…");emit changed();return true;
}
QVariantList TerminalController::commands(int mode,const QString &search,const QString &category) const
{
    QVariantList result;if(mode<0||mode>=systems.size())return result;
    for(const auto &entry:m_commands){
        const auto map=entry.toMap();if(map.value("system").toString()!=systems.at(mode))continue;
        if(!category.isEmpty()&&category!="全部"&&category!=map.value("category").toString())continue;
        QString searchable;for(const auto &value:map)searchable+=value.toString()+" ";
        if(!search.trimmed().isEmpty()&&!searchable.contains(search.trimmed(),Qt::CaseInsensitive))continue;
        result.append(entry);
    }return result;
}
int TerminalController::inputHelper(const QByteArray &request)
{
    if(request.size()>32768)return 1;
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(request,&error);
    if(error.error!=QJsonParseError::NoError||!doc.isObject())return 1;
    const auto object=doc.object();const auto pid=object.value("pid").toInteger();
    const auto kind=object.value("kind").toString(),value=object.value("value").toString();
    if(pid<=0||pid>0xffffffffLL||(kind!="text"&&kind!="key")||(kind=="text"&&!validText(value))||(kind=="key"&&!keys.contains(value)))return 1;
#ifdef Q_OS_WIN
    auto target=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(pid));
    if(!target)return 2;
    const auto started=object.value("started").toString().toULongLong();
    const bool alive=started!=0&&creationTime(target)==started&&WaitForSingleObject(target,0)==WAIT_TIMEOUT;
    if(!alive){CloseHandle(target);return 2;}
    FreeConsole();
    // Windows 控制台宿主可能稍晚于目标进程就绪；只重试附着，不重放输入。
    QElapsedTimer wait;wait.start();DWORD attachError=0;
    while(!AttachConsole(DWORD(pid))){
        attachError=GetLastError();
        if(attachError!=ERROR_INVALID_HANDLE||wait.elapsed()>=2000||WaitForSingleObject(target,0)!=WAIT_TIMEOUT){
            fprintf(stderr,"AttachConsole error=%lu",attachError);CloseHandle(target);return 3;
        }
        QThread::msleep(25);
    }
    SetConsoleCtrlHandler(nullptr,TRUE);
    auto console=CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    if(console==INVALID_HANDLE_VALUE){fprintf(stderr,"CONIN error=%lu",GetLastError());FreeConsole();CloseHandle(target);return 3;}
    DWORD inputMode=0;
    if(!GetConsoleMode(console,&inputMode)){CloseHandle(console);FreeConsole();CloseHandle(target);return 3;}
    // SSH/ADB 原始模式必须收到字符 0x03；处理模式才广播本机中断事件。
    if(kind=="key"&&value=="ctrlC"&&(inputMode&ENABLE_PROCESSED_INPUT)){
        const bool ok=GenerateConsoleCtrlEvent(CTRL_C_EVENT,0);
        CloseHandle(console);FreeConsole();CloseHandle(target);return ok?0:4;
    }
    QList<INPUT_RECORD> events;
    auto append=[&events](wchar_t character,WORD vk,DWORD state){
        INPUT_RECORD record{};record.EventType=KEY_EVENT;auto &event=record.Event.KeyEvent;
        event.bKeyDown=TRUE;event.wRepeatCount=1;event.wVirtualKeyCode=vk;
        event.wVirtualScanCode=WORD(MapVirtualKeyW(vk,MAPVK_VK_TO_VSC));
        event.uChar.UnicodeChar=character;event.dwControlKeyState=state;events.append(record);
        event.bKeyDown=FALSE;events.append(record);
    };
    if(kind=="text")for(const auto c:value)append(wchar_t(c.unicode()),0,0);
    else if(value=="enter")append(L'\r',VK_RETURN,0);
    else if(value=="tab")append(L'\t',VK_TAB,0);
    else if(value=="escape")append(27,VK_ESCAPE,0);
    else if(value=="backspace")append(L'\b',VK_BACK,0);
    else if(value=="ctrlD")append(4,'D',LEFT_CTRL_PRESSED);
    else if(value=="ctrlC")append(3,'C',LEFT_CTRL_PRESSED);
    else append(0,value=="up"?VK_UP:value=="down"?VK_DOWN:value=="left"?VK_LEFT:VK_RIGHT,ENHANCED_KEY);
    DWORD written=0;const DWORD count=DWORD(events.size());
    const bool ok=WriteConsoleInputW(console,events.constData(),count,&written)&&written==count;
    CloseHandle(console);FreeConsole();CloseHandle(target);return ok?0:4;
#else
    return 5;
#endif
}
