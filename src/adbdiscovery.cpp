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
}
AdbDiscovery::~AdbDiscovery()
{
    m_poll.stop();m_timeout.stop();m_process.disconnect(this);
    if(m_process.state()!=QProcess::NotRunning){m_process.kill();m_process.waitForFinished(1000);}
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
    m_path=local;QSettings().setValue("terminal/adbPath",m_path);
    m_devices.clear();m_selected.clear();m_resolved.clear();
    if(m_scanning){m_restart=true;m_process.kill();}
    else if(m_enabled)refresh();
    emit changed();
}
void AdbDiscovery::setSelectedSerial(const QString &serial)
{
    if(serial==m_selected)return;
    for(const auto &entry:m_devices)if(entry.toMap().value("serial")==serial){m_selected=serial;emit changed();return;}
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
    if(!keep)m_selected=online==1?onlyOnline:QString();
    m_status=m_devices.isEmpty()?QStringLiteral("未检测到 ADB 设备，请检查 USB 数据线、板端 USB 调试和驱动"):
        QStringLiteral("检测到 %1 台设备，%2 台可连接").arg(m_devices.size()).arg(online);
    if(online>1&&m_selected.isEmpty())m_status+=QStringLiteral("；请选择目标设备");
    if(unauthorized)m_status+=QStringLiteral("；请在开发板屏幕确认 USB 调试授权");
    if(offline)m_status+=QStringLiteral("；离线设备请检查连接或重新插拔");
    emit changed();
}
