#pragma once
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QProcessEnvironment>

// 供工作线程与终端共用，只解析路径，不启动设备命令。
namespace ToolPaths {
QString root();
void configure(const QString &root, const QVariantMap &overrides);
bool hasOverride(const QString &name);
QProcessEnvironment environment();
QString windowsEnvironmentBlock();
QString resolve(const QString &name, const QString &explicitPath=QString());
QString locate(const QString &name,const QString &explicitPath,const QString &managedRoot,
               const QString &applicationRoot,const QStringList &searchPaths);
}
