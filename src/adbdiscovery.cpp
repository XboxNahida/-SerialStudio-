#include "adbdiscovery.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

QString AdbDiscovery::resolveExecutable(const QString &path)
{
    QStringList candidates;
    if(!path.trimmed().isEmpty())candidates<<path.trimmed();
    else {
        const QDir app(QCoreApplication::applicationDirPath());
        candidates<<app.filePath("platform-tools/adb.exe")<<app.filePath("adb.exe")<<QStandardPaths::findExecutable("adb.exe");
        for(const auto &key:{"ANDROID_SDK_ROOT","ANDROID_HOME"}){
            const auto sdk=qEnvironmentVariable(key);if(!sdk.isEmpty())candidates<<QDir(sdk).filePath("platform-tools/adb.exe");
        }
        const auto local=qEnvironmentVariable("LOCALAPPDATA");
        if(!local.isEmpty())candidates<<QDir(local).filePath("Android/Sdk/platform-tools/adb.exe");
    }
    for(const auto &candidate:candidates)if(!candidate.isEmpty()&&QFileInfo(candidate).isFile())return QFileInfo(candidate).absoluteFilePath();
    return {};
}
AdbDiscovery::AdbDiscovery(QObject *parent):QObject(parent)
{
    m_path=QSettings().value("terminal/adbPath").toString();
    m_poll.setInterval(3000);m_timeout.setInterval(6000);m_timeout.setSingleShot(true);
    connect(&m_poll,&QTimer::timeout,this,&AdbDiscovery::refresh);
    connect(&m_timeout,&QTimer::timeout,this,[this]{m_failure=QStringLiteral("ADB 检测超时，请检查工具或 USB 连接");m_process.kill();});
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&AdbDiscovery::collect);
    connect(&m_process,&QProcess::readyReadStandardError,this,&AdbDiscovery::collect);
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,&AdbDiscovery::complete);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart){m_failure=QStringLiteral("无法运行 adb.exe：%1").arg(m_process.errorString());complete(-1,QProcess::CrashExit);}
    });
    m_connectionTimeout.setSingleShot(true);m_connectionTimeout.setInterval(15000);
    connect(&m_connectionTimeout,&QTimer::timeout,this,[this]{
        m_connectionFailure=QStringLiteral("ADB 网络操作超时");m_connectionProcess.kill();
    });
    connect(&m_connectionProcess,&QProcess::readyReadStandardOutput,this,&AdbDiscovery::collectConnection);
    connect(&m_connectionProcess,&QProcess::readyReadStandardError,this,&AdbDiscovery::collectConnection);
    connect(&m_connectionProcess,&QProcess::started,this,[this]{
        if(m_connectionOperation=="pair"){
            m_connectionProcess.write(m_pairCode.toUtf8()+"\n");m_pairCode.clear();m_connectionProcess.closeWriteChannel();
        }
    });
    connect(&m_connectionProcess,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,&AdbDiscovery::completeConnection);
    connect(&m_connectionProcess,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart){m_connectionFailure=QStringLiteral("无法启动 ADB 工具");completeConnection(-1,QProcess::CrashExit);}
    });
}
AdbDiscovery::~AdbDiscovery()
{
    m_poll.stop();m_timeout.stop();m_process.disconnect(this);
    if(m_process.state()!=QProcess::NotRunning){m_process.kill();m_process.waitForFinished(1000);}
    m_connectionTimeout.stop();m_connectionProcess.disconnect(this);
    if(m_connectionProcess.state()!=QProcess::NotRunning){m_connectionProcess.kill();m_connectionProcess.waitForFinished(1000);}
}
bool AdbDiscovery::ready() const
{
    for(const auto &entry:m_devices){const auto item=entry.toMap();if(item.value("serial")==m_selected&&item.value("state")=="device")return true;}
    return false;
}
void AdbDiscovery::setExecutablePath(const QString &path)
{
    const QUrl url(path);const auto local=url.isLocalFile()?url.toLocalFile():path.trimmed();
    if(local==m_path)return;
    m_path=local;if(m_persistenceEnabled)QSettings().setValue("terminal/adbPath",m_path);
    if(m_connectionBusy){
        // 旧工具返回的结果不得选择或刷新新工具下的目标。
        m_connectionFailure=QStringLiteral("ADB 工具已更换，网络操作已取消");
        m_connectionEndpoint.clear();m_pairCode.clear();m_connectionProcess.kill();
    }
    m_devices.clear();m_selected.clear();m_resolved.clear();
    if(m_scanning){m_restart=true;m_process.kill();}
    else if(m_enabled)refresh();
    emit changed();
}
bool AdbDiscovery::connectDevice(QString endpoint) {return startConnection("connect",endpoint);}
bool AdbDiscovery::pairDevice(QString endpoint,QString code) {return startConnection("pair",endpoint,code);}
bool AdbDiscovery::disconnectDevice(QString endpoint) {return startConnection("disconnect",endpoint);}
bool AdbDiscovery::startConnection(const QString &operation,const QString &endpoint,const QString &code)
{
    if(m_connectionBusy)return false;
    const QRegularExpression pattern("^([A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?):([0-9]{1,5})$");
    const auto match=pattern.match(endpoint);
    const auto port=match.hasMatch()?match.captured(2).toUInt():0;
    bool validHost=match.hasMatch();
    if(validHost){
        const auto host=match.captured(1);
        const auto labels=host.split('.');
        for(const auto &label:labels)
            if(label.isEmpty()||label.size()>63||label.startsWith('-')||label.endsWith('-'))validHost=false;
        if(QRegularExpression("^[0-9.]+$").match(host).hasMatch()){
            if(labels.size()!=4)validHost=false;
            for(const auto &label:labels)if(label.toUInt()>255)validHost=false;
        }
    }
    if(!validHost||endpoint.size()>256||port<1||port>65535||endpoint.contains('\n')||endpoint.contains('\r')){
        m_connectionStatus=QStringLiteral("地址无效：请填写 IPv4 或域名:端口（1–65535），暂不支持 IPv6");emit changed();return false;
    }
    if(operation=="pair"&&(code.size()!=6||!QRegularExpression("^[0-9]{6}$").match(code).hasMatch())){
        m_connectionStatus=QStringLiteral("配对码必须为设备显示的六位数字");emit changed();return false;
    }
    const auto executable=resolveExecutable(m_path);
    if(executable.isEmpty()){
        m_connectionStatus=QStringLiteral("网络 ADB 失败：请先选择有效的 adb.exe");emit changed();return false;
    }
    m_connectionOperation=operation;m_connectionEndpoint=endpoint;m_pairCode=code;
    m_connectionOutput.clear();m_connectionFailure.clear();m_connectionBusy=true;
    m_connectionStatus=operation=="pair"?QStringLiteral("正在无线配对…"):operation=="disconnect"?QStringLiteral("正在断开网络 ADB…"):QStringLiteral("正在连接网络 ADB…");
    emit changed();m_connectionTimeout.start();m_connectionProcess.start(executable,{operation,endpoint});return true;
}
void AdbDiscovery::collectConnection()
{
    const auto output=m_connectionProcess.readAllStandardOutput(),errors=m_connectionProcess.readAllStandardError();
    constexpr qsizetype limit=65536;
    if(output.size()>limit-m_connectionOutput.size()||errors.size()>limit-m_connectionOutput.size()-output.size()){
        m_connectionFailure=QStringLiteral("ADB 网络操作失败：输出超过 64 KiB");m_connectionProcess.kill();return;
    }
    m_connectionOutput+=output;m_connectionOutput+=errors;
}
void AdbDiscovery::completeConnection(int code,QProcess::ExitStatus exit)
{
    if(!m_connectionBusy)return;
    m_connectionTimeout.stop();collectConnection();m_connectionBusy=false;m_pairCode.clear();
    const auto output=QString::fromUtf8(m_connectionOutput);
    const bool failed=QRegularExpression("\\b(failed|failure|cannot|unable|error|refused|denied|invalid|timed out)\\b",QRegularExpression::CaseInsensitiveOption).match(output).hasMatch();
    const bool success=m_connectionFailure.isEmpty()&&exit==QProcess::NormalExit&&code==0&&!failed;
    if(!success){
        // 配对工具可能回显凭据，任何配对输出都不进入状态、日志或配置。
        m_connectionStatus=!m_connectionFailure.isEmpty()?m_connectionFailure:
            m_connectionOperation=="pair"?QStringLiteral("无线配对失败，请核对地址、配对码与设备提示"):
            QStringLiteral("ADB 网络操作失败：%1").arg(output.trimmed().left(400));
    }else if(m_connectionOperation=="pair")m_connectionStatus=QStringLiteral("无线配对成功；请使用设备的调试连接端口点击连接");
    else {
        m_connectionStatus=m_connectionOperation=="connect"?QStringLiteral("网络 ADB 已连接：%1").arg(m_connectionEndpoint):QStringLiteral("网络 ADB 已断开：%1").arg(m_connectionEndpoint);
        if(m_connectionOperation=="connect")setSelectedSerial(m_connectionEndpoint);
        if(m_scanning)m_refreshAfterScan=true;else refresh();
    }
    m_connectionOutput.clear();m_connectionEndpoint.clear();emit changed();
}
void AdbDiscovery::setSelectedSerial(const QString &serial)
{
    static const QRegularExpression pattern("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,255}$");
    if(!serial.isEmpty()&&!pattern.match(serial).hasMatch())return;
    if(serial==m_selected&&serial==m_pendingSerial)return;
    // 显式目标在首次枚举前暂存，断线时也保留，避免自动切到另一块板。
    m_pendingSerial=serial;m_selected.clear();
    for(const auto &entry:m_devices)if(entry.toMap().value("serial")==serial){m_selected=serial;break;}
    emit changed();
}
void AdbDiscovery::setPersistenceEnabled(bool enabled)
{
    if(enabled==m_persistenceEnabled)return;
    m_persistenceEnabled=enabled;emit changed();
}
void AdbDiscovery::setEnabled(bool enabled)
{
    if(enabled==m_enabled)return;
    m_enabled=enabled;
    if(enabled){m_poll.start();refresh();}else m_poll.stop();
    emit changed();
}
void AdbDiscovery::refresh()
{
    if(m_scanning)return;
    m_resolved=resolveExecutable(m_path);
    if(m_resolved.isEmpty()){
        m_devices.clear();m_selected.clear();
        m_status=m_path.isEmpty()?QStringLiteral("未找到 adb.exe：请选择 Android SDK 的 platform-tools/adb.exe"):QStringLiteral("指定的 adb.exe 不存在，请重新选择或清空以自动查找");
        emit changed();return;
    }
    m_output.clear();m_errors.clear();m_failure.clear();m_scanning=true;
    m_status=QStringLiteral("正在检测 Android USB / ADB 设备…");emit changed();
    m_process.start(m_resolved,{"devices","-l"});m_timeout.start();
}
void AdbDiscovery::collect()
{
    // 丢弃超出上限的输出，避免错误工具持续输出占用内存。
    const auto output=m_process.readAllStandardOutput(),errors=m_process.readAllStandardError();
    constexpr qsizetype limit=65536;
    if(output.size()>limit-m_output.size()||errors.size()>limit-m_errors.size()){
        m_failure=QStringLiteral("ADB 输出超过 64 KiB，已停止检测");m_process.kill();return;
    }
    m_output+=output;m_errors+=errors;
}
void AdbDiscovery::complete(int code,QProcess::ExitStatus exit)
{
    if(!m_scanning)return;
    m_timeout.stop();collect();m_scanning=false;
    if(m_restart){m_restart=false;if(m_enabled)refresh();else emit changed();return;}
    if(m_refreshAfterScan){m_refreshAfterScan=false;refresh();return;}
    m_devices.clear();
    if(!m_failure.isEmpty()||code!=0||exit!=QProcess::NormalExit){
        m_selected.clear();m_status=!m_failure.isEmpty()?m_failure:QStringLiteral("ADB 检测失败（%1）：%2").arg(code).arg(QString::fromUtf8(m_errors).trimmed().left(400));
        emit changed();return;
    }
    const QRegularExpression serialPattern("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,255}$");
    int online=0;QString onlyOnline;bool keep=false,unauthorized=false,offline=false;
    for(const auto &line:QString::fromUtf8(m_output).split('\n')){
        const auto fields=line.trimmed().split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
        if(fields.size()<2||!serialPattern.match(fields[0]).hasMatch())continue;
        const auto serial=fields[0],state=fields[1];
        if(!QStringList{"device","unauthorized","offline","recovery","sideload","bootloader","no"}.contains(state))continue;
        QString model;
        for(const auto &field:fields)if(field.startsWith("model:"))model=field.mid(6).replace('_',' ');
        const auto label=state=="device"?QStringLiteral("已连接"):state=="unauthorized"?QStringLiteral("未授权"):state=="offline"?QStringLiteral("离线"):state=="no"?QStringLiteral("无权限"):state;
        m_devices.append(QVariantMap{{"serial",serial},{"state",state},{"model",model},{"label",(model.isEmpty()?serial:model+" · "+serial)+" · "+label}});
        if(state=="device"){++online;onlyOnline=serial;if(serial==m_selected)keep=true;}
        unauthorized|=state=="unauthorized";offline|=state=="offline";
    }
    if(!m_pendingSerial.isEmpty()){
        m_selected.clear();
        for(const auto &entry:m_devices){
            const auto item=entry.toMap();
            if(item.value("serial")==m_pendingSerial&&item.value("state")=="device"){
                m_selected=m_pendingSerial;break;
            }
        }
    }else if(!keep)m_selected=online==1?onlyOnline:QString();
    m_status=m_devices.isEmpty()?QStringLiteral("未检测到 ADB 设备，请检查 USB 数据线、板端 USB 调试和驱动"):
        QStringLiteral("检测到 %1 台设备，%2 台可连接").arg(m_devices.size()).arg(online);
    if(online>1&&m_selected.isEmpty())m_status+=QStringLiteral("；请选择目标设备");
    if(!m_pendingSerial.isEmpty()&&m_selected.isEmpty())m_status+=QStringLiteral("；等待所选设备 %1").arg(m_pendingSerial);
    if(unauthorized)m_status+=QStringLiteral("；请在开发板屏幕确认 USB 调试授权");
    if(offline)m_status+=QStringLiteral("；离线设备请检查连接或重新插拔");
    emit changed();
}
