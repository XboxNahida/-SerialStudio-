#include "toolpaths.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QReadWriteLock>
#include <QVersionNumber>
#include <QUrl>
#include <algorithm>

namespace { QReadWriteLock lock; QString managed; QVariantMap custom; }
QString ToolPaths::root() {
    QReadLocker guard(&lock);
    return managed.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/SerialStudio/tools":managed;
}
void ToolPaths::configure(const QString &directory,const QVariantMap &overrides) {
    QWriteLocker guard(&lock); managed=directory;custom=overrides;
}
bool ToolPaths::hasOverride(const QString &name) {QReadLocker guard(&lock);return !custom.value(name).toString().isEmpty();}
QProcessEnvironment ToolPaths::environment() {
    auto env=QProcessEnvironment::systemEnvironment();QStringList directories;
    for(const auto &name:{"python","adb","fastboot","ssh","sftp","scp","openocd","dfu-util","jlink","stm32cubeprogrammer"}) {
        const auto program=resolve(name);if(program.isEmpty())continue;
        const auto directory=QDir::toNativeSeparators(QFileInfo(program).absolutePath());
        if(!directories.contains(directory,Qt::CaseInsensitive))directories.append(directory);
    }
    if(!directories.isEmpty())env.insert("PATH",directories.join(QDir::listSeparator())+QDir::listSeparator()+env.value("PATH"));
    return env;
}
QString ToolPaths::windowsEnvironmentBlock() {
    auto items=environment().toStringList();
    std::sort(items.begin(),items.end(),[](const QString &a,const QString &b){return a.compare(b,Qt::CaseInsensitive)<0;});
    return items.join(QChar(0))+QChar(0)+QChar(0);
}
QString ToolPaths::locate(const QString &name,const QString &explicitPath,const QString &managedRoot,const QString &applicationRoot,const QStringList &searchPaths) {
    const QMap<QString,QPair<QString,QString>> tools{
        {"adb",{"android-platform-tools","platform-tools/adb.exe"}},
        {"fastboot",{"android-platform-tools","platform-tools/fastboot.exe"}},
        {"ssh",{"openssh","OpenSSH-Win64/ssh.exe"}}, {"sftp",{"openssh","OpenSSH-Win64/sftp.exe"}},
        {"scp",{"openssh","OpenSSH-Win64/scp.exe"}}, {"python",{"python","python.exe"}},
        {"openocd",{"openocd","xpack-openocd-0.12.0-7/bin/openocd.exe"}},
        {"dfu-util",{"dfu-util","dfu-util.exe"}}, {"jlink",{"jlink","JLink.exe"}},
        {"stm32cubeprogrammer",{"stm32cubeprogrammer","STM32_Programmer_CLI.exe"}}
    };
    // Windows 应用执行别名可能显示为零字节 exe，不能据此当作可运行的解释器。
    const auto file=[](const QString &path) { const QFileInfo info(path);return info.isFile()&&info.size()>0?info.absoluteFilePath():QString(); };
    if(!tools.contains(name))return {};
    if(!explicitPath.trimmed().isEmpty()) {
        const auto url=QUrl(explicitPath.trimmed());
        return file(url.isLocalFile()?url.toLocalFile():explicitPath.trimmed());
    }
    const auto layout=tools.value(name);
    const QDir component(QDir(managedRoot).filePath(layout.first));
    auto versions=component.entryList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks);
    std::sort(versions.begin(),versions.end(),[](const QString &a,const QString &b) {return QVersionNumber::fromString(a)>QVersionNumber::fromString(b);});
    for(const auto &version:versions) {
        const auto candidate=file(component.filePath(version+'/'+layout.second));
        if(!candidate.isEmpty())return candidate;
    }
    // 兼容既有便携版位置，不从任意目录递归搜索程序。
    if(name=="adb" || name=="fastboot") {
        for(const auto &relative:{"platform-tools/"+name+".exe",name+".exe"}) {
            const auto candidate=file(QDir(applicationRoot).filePath(relative));if(!candidate.isEmpty())return candidate;
        }
    }
    if(name=="jlink")return {}; // PATH 中 jlink.exe 可能是 Java 模块工具。
    const auto executable=name=="stm32cubeprogrammer"?QStringLiteral("STM32_Programmer_CLI.exe"):name+".exe";
    const auto directories=searchPaths.isEmpty()?qEnvironmentVariable("PATH").split(QDir::listSeparator(),Qt::SkipEmptyParts):searchPaths;
    for(const auto &directory:directories) {
        const auto candidate=file(QStandardPaths::findExecutable(executable,{directory}));
        if(!candidate.isEmpty())return candidate;
    }
    return {};
}
QString ToolPaths::resolve(const QString &name,const QString &explicitPath) {
    QString path=explicitPath;
    { QReadLocker guard(&lock); if(path.isEmpty()) path=custom.value(name).toString(); }
    return locate(name,path,root(),QCoreApplication::applicationDirPath(),qEnvironmentVariable("PATH").split(QDir::listSeparator(),Qt::SkipEmptyParts));
}
