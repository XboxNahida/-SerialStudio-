#pragma once

#include <QString>
#include <QVariantList>
#include <atomic>
#include <functional>

// Windows Shell 的便携设备文件接口。每次调用只在当前线程创建、使用和释放 COM 对象。
// 应由后台 STA 工作线程调用；id 是 Shell 绝对解析路径，本地绝对路径也可用作夹具。
namespace PortableFiles {
struct Result {
    QVariantList entries;
    QString error;
};
Result roots();
Result list(const QString &id);
// 仅复制单个文件，禁止覆盖。空字符串代表成功，否则是中文错误信息。
// 回调在调用线程执行；取消/驱动失败可能留下部分文件，本接口不会删除文件。
QString copy(const QString &sourceId, const QString &destinationFolderId,
             std::atomic_bool *cancelled = nullptr,
             std::function<void(int)> progress = {});
// 由界面明确确认后调用；永久删除，目录包含子项，不跟随文件系统符号链接。
QString remove(const QString &sourceId, std::atomic_bool *cancelled = nullptr);
}
