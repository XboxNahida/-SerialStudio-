#include "pseudoterminal.h"
#include "toolpaths.h"
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#ifdef Q_OS_WIN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006
#endif
#include <windows.h>
#include <processthreadsapi.h>
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif
#endif

struct PseudoTerminal::Native {
#ifdef Q_OS_WIN
    HPCON console=nullptr;
    HANDLE process=nullptr,input=nullptr,output=nullptr,job=nullptr;
    std::thread reader,writer;
    QMutex mutex;
    QByteArray pending;
    std::mutex inputMutex;
    std::condition_variable inputReady;
    std::deque<QByteArray> inputQueue;
    qsizetype queuedInput=0;
    std::atomic_bool stop{false};
    std::atomic_bool writerDone{true};
    std::atomic_bool overflow{false};
    std::atomic<DWORD> writeError{0};
#endif
};
namespace {
#ifdef Q_OS_WIN
QString quote(const QString &value) {
    QString result="\"";int slashes=0;
    for(const auto character:value){
        if(character=='\\'){++slashes;continue;}
        result+=QString(character=='\"'?slashes*2+1:slashes,'\\');
        result+=character;slashes=0;
    }return result+QString(slashes*2,'\\')+'\"';
}
void release(HANDLE &handle){if(handle && handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);handle=nullptr;}
#endif
}
PseudoTerminal::PseudoTerminal(QObject *parent):QObject(parent),m_native(std::make_unique<Native>()) {
    m_poll.setInterval(16);
    connect(&m_poll,&QTimer::timeout,this,[this]{
#ifdef Q_OS_WIN
        QByteArray bytes;
        {QMutexLocker locker(&m_native->mutex);bytes.swap(m_native->pending);}
        if(!bytes.isEmpty())emit received(bytes);
        if(m_native->overflow.exchange(false)){m_error=QStringLiteral("终端输出过快，已丢弃超出 8 MiB 缓冲的内容");emit fault(m_error);}
        if(const auto error=m_native->writeError.exchange(0)){
            m_error=QStringLiteral("终端输入失败：%1").arg(error);emit fault(m_error);
        }
        if(m_native->process && WaitForSingleObject(m_native->process,0)==WAIT_OBJECT_0){
            DWORD code=0;GetExitCodeProcess(m_native->process,&code);close();emit finished(int(code));
        }
#endif
    });
}
PseudoTerminal::~PseudoTerminal(){close();}
bool PseudoTerminal::running() const {
#ifdef Q_OS_WIN
    return m_native->process && WaitForSingleObject(m_native->process,0)==WAIT_TIMEOUT;
#else
    return false;
#endif
}
bool PseudoTerminal::start(const QString &program,const QStringList &arguments,int columns,int rows) {
    close();m_error.clear();
    if(!QFileInfo(program).isFile()){m_error=QStringLiteral("终端程序不存在");return false;}
#ifdef Q_OS_WIN
    HANDLE inputRead=nullptr,outputWrite=nullptr;
    if(!CreatePipe(&inputRead,&m_native->input,nullptr,0)||!CreatePipe(&m_native->output,&outputWrite,nullptr,0)){
        m_error=QStringLiteral("无法创建终端管道：%1").arg(GetLastError());release(inputRead);release(outputWrite);close();return false;
    }
    const COORD size{SHORT(qBound(10,columns,400)),SHORT(qBound(3,rows,160))};
    const auto hr=CreatePseudoConsole(size,inputRead,outputWrite,0,&m_native->console);
    if(FAILED(hr)){release(inputRead);release(outputWrite);m_error=QStringLiteral("无法创建 ConPTY（需要 Windows 10 1809+）：%1").arg(quint32(hr));close();return false;}
    SIZE_T attributeSize=0;InitializeProcThreadAttributeList(nullptr,1,0,&attributeSize);
    std::vector<unsigned char> storage(attributeSize);
    auto *attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!InitializeProcThreadAttributeList(attributes,1,0,&attributeSize)){
        release(inputRead);release(outputWrite);m_error=QStringLiteral("无法初始化终端进程属性");close();return false;
    }
    if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,m_native->console,sizeof(HPCON),nullptr,nullptr)){
        release(inputRead);release(outputWrite);DeleteProcThreadAttributeList(attributes);m_error=QStringLiteral("无法附加终端进程属性");close();return false;
    }
    QString command=quote(QDir::toNativeSeparators(program));
    for(const auto &argument:arguments)command+=' '+quote(argument);
    auto native=command.toStdWString();
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
    // 避免把宿主的 NUL/日志管道传给 Shell；NULL 由 ConPTY 附着时补为控制台句柄。
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    PROCESS_INFORMATION process{};
    const auto environment=ToolPaths::windowsEnvironmentBlock();
    const bool created=CreateProcessW(reinterpret_cast<LPCWSTR>(program.utf16()),native.data(),nullptr,nullptr,FALSE,
        EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT,
        const_cast<ushort *>(environment.utf16()),nullptr,&startup.StartupInfo,&process);
    const auto error=GetLastError();DeleteProcThreadAttributeList(attributes);
    if(!created){release(inputRead);release(outputWrite);m_error=QStringLiteral("终端进程启动失败：%1").arg(error);close();return false;}
    m_native->process=process.hProcess;m_native->job=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!m_native->job || !SetInformationJobObject(m_native->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)) || !AssignProcessToJobObject(m_native->job,process.hProcess)){
        release(inputRead);release(outputWrite);TerminateProcess(process.hProcess,1);release(process.hThread);m_error=QStringLiteral("无法隔离终端子进程");close();return false;
    }
    m_native->stop=false;m_native->overflow=false;m_native->writeError=0;
    m_native->reader=std::thread([native=m_native.get()]{
        char buffer[16384];DWORD received=0;
        // ClosePseudoConsole 可产生最后一帧，必须持续排空直到 EOF。
        while(ReadFile(native->output,buffer,sizeof(buffer),&received,nullptr) && received){
            QMutexLocker locker(&native->mutex);
            if(native->pending.size()+received<=8*1024*1024)native->pending.append(buffer,qsizetype(received));
            else native->overflow=true;
        }
    });
    m_native->writerDone=false;
    m_native->writer=std::thread([native=m_native.get()]{
        for(;;){
            QByteArray bytes;
            {
                std::unique_lock lock(native->inputMutex);
                native->inputReady.wait(lock,[&]{return native->stop || !native->inputQueue.empty();});
                if(native->stop)break;
                bytes=std::move(native->inputQueue.front());native->inputQueue.pop_front();
            }
            qsizetype offset=0;
            while(offset<bytes.size() && !native->stop){
                DWORD written=0;
                if(!WriteFile(native->input,bytes.constData()+offset,DWORD(bytes.size()-offset),&written,nullptr) || !written){
                    if(!native->stop)native->writeError=GetLastError();
                    native->stop=true;break;
                }
                offset+=qsizetype(written);
            }
            {std::lock_guard lock(native->inputMutex);native->queuedInput-=bytes.size();}
        }
        native->writerDone=true;
    });
    ResumeThread(process.hThread);release(process.hThread);release(inputRead);release(outputWrite);m_poll.start();return true;
#else
    Q_UNUSED(arguments) Q_UNUSED(columns) Q_UNUSED(rows)
    m_error=QStringLiteral("此版本的内置终端仅在 Windows 上可用");return false;
#endif
}
bool PseudoTerminal::write(const QByteArray &bytes) {
#ifdef Q_OS_WIN
    if(!running()||bytes.size()>65536)return false;
    std::lock_guard lock(m_native->inputMutex);
    // 含正在写入的数据，有界队列保证大段粘贴不无限占用内存。
    if(m_native->stop || bytes.size()>1024*1024-m_native->queuedInput)return false;
    if(bytes.isEmpty())return true;
    m_native->queuedInput+=bytes.size();m_native->inputQueue.push_back(bytes);
    m_native->inputReady.notify_one();return true;
#else
    Q_UNUSED(bytes) return false;
#endif
}
void PseudoTerminal::resize(int columns,int rows) {
#ifdef Q_OS_WIN
    if(m_native->console)ResizePseudoConsole(m_native->console,{SHORT(qBound(10,columns,400)),SHORT(qBound(3,rows,160))});
#else
    Q_UNUSED(columns) Q_UNUSED(rows)
#endif
}
void PseudoTerminal::close() {
    m_poll.stop();
#ifdef Q_OS_WIN
    {std::lock_guard lock(m_native->inputMutex);m_native->stop=true;}
    m_native->inputReady.notify_one();
    if(m_native->writer.joinable()){
        // 重复取消覆盖“检查 stop 后、进入同步 WriteFile 前”的竞态窗口。
        while(!m_native->writerDone){
            CancelSynchronousIo(reinterpret_cast<HANDLE>(m_native->writer.native_handle()));
            Sleep(1);
        }
        m_native->writer.join();
    }
    if(m_native->job)release(m_native->job);
    if(m_native->process && WaitForSingleObject(m_native->process,0)==WAIT_TIMEOUT)TerminateProcess(m_native->process,0);
    release(m_native->input);
    if(m_native->console){ClosePseudoConsole(m_native->console);m_native->console=nullptr;}
    if(m_native->reader.joinable())m_native->reader.join();
    release(m_native->output);release(m_native->process);
    {std::lock_guard lock(m_native->inputMutex);m_native->inputQueue.clear();m_native->queuedInput=0;}
    QByteArray finalBytes;
    {QMutexLocker locker(&m_native->mutex);finalBytes.swap(m_native->pending);}
    if(!finalBytes.isEmpty())emit received(finalBytes);
#endif
}
