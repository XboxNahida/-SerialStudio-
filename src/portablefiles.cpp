#include "portablefiles.h"

#include <QDir>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <sherrors.h>

namespace {
const QString pidlPrefix = QStringLiteral("shell-pidl:");
constexpr qsizetype maxPidlBytes = 1024 * 1024;
template<class T> class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { if (ptr) ptr->Release(); }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    T *operator->() const { return ptr; }
    T *get() const { return ptr; }
    T **put() { return &ptr; }
private:
    T *ptr = nullptr;
};
struct TaskMemory {
    void *ptr = nullptr;
    ~TaskMemory() { CoTaskMemFree(ptr); }
};
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
QString failure(const QString &action, HRESULT result)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
        | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(result), 0,
        reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    const QString detail = length ? QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed() : QString();
    LocalFree(buffer);
    return QStringLiteral("%1（0x%2）%3").arg(action,
        QString::number(static_cast<quint32>(result), 16).rightJustified(8, QLatin1Char('0')), detail);
}
QString apartmentError(HRESULT result)
{
    if (result == RPC_E_CHANGED_MODE)
        return QStringLiteral("Windows Shell 文件操作需要独立的 STA 后台线程，当前线程已初始化为其他 COM 模式");
    return failure(QStringLiteral("无法初始化 Windows Shell"), result);
}
bool validId(const QString &id)
{
    if (id.isEmpty() || id.contains(QChar::Null) || id.contains(QLatin1Char('\n')) || id.contains(QLatin1Char('\r')))
        return false;
    return QDir::isAbsolutePath(id) || id.startsWith(QStringLiteral("::{"))
        || id.startsWith(pidlPrefix);
}
HRESULT makeItem(const QString &id, ComPtr<IShellItem> &item)
{
    if (id.startsWith(pidlPrefix)) {
        if (id.size() - pidlPrefix.size() > ((maxPidlBytes + 2) / 3) * 4) return E_INVALIDARG;
        const auto bytes = QByteArray::fromBase64(id.mid(pidlPrefix.size()).toLatin1(),
                                                 QByteArray::AbortOnBase64DecodingErrors);
        if (bytes.size() < 2 || bytes.size() > maxPidlBytes) return E_INVALIDARG;
        // PIDL 是变长项目链；先验证每段长度和结束标记，不能把任意输入直接交给 Shell。
        qsizetype offset = 0;
        for (;;) {
            const auto remaining = bytes.size() - offset;
            if (remaining < 2) return E_INVALIDARG;
            const auto size = qFromLittleEndian<quint16>(bytes.constData() + offset);
            if (size == 0) {
                if (remaining != 2) return E_INVALIDARG;
                break;
            }
            if (size < 2 || size > remaining) return E_INVALIDARG;
            offset += size;
        }
        TaskMemory pidl;
        pidl.ptr = CoTaskMemAlloc(static_cast<SIZE_T>(bytes.size()));
        if (!pidl.ptr) return E_OUTOFMEMORY;
        std::memcpy(pidl.ptr, bytes.constData(), static_cast<size_t>(bytes.size()));
        return SHCreateItemFromIDList(static_cast<PCIDLIST_ABSOLUTE>(pidl.ptr), IID_PPV_ARGS(item.put()));
    }
    const auto native = QDir::toNativeSeparators(id).toStdWString();
    return SHCreateItemFromParsingName(native.c_str(), nullptr, IID_PPV_ARGS(item.put()));
}
QString nameOf(IShellItem *item, SIGDN kind, HRESULT *error = nullptr)
{
    TaskMemory memory;
    const HRESULT result = item->GetDisplayName(kind, reinterpret_cast<PWSTR *>(&memory.ptr));
    if (error) *error = result;
    return SUCCEEDED(result) && memory.ptr ? QString::fromWCharArray(static_cast<wchar_t *>(memory.ptr)) : QString();
}
QString idOf(IShellItem *item, HRESULT *error = nullptr)
{
    SFGAOF attributes = 0;
    HRESULT result = item->GetAttributes(SFGAO_FILESYSTEM, &attributes);
    if (FAILED(result)) {
        if (error) *error = result;
        return {};
    }
    if (attributes & SFGAO_FILESYSTEM) return nameOf(item, SIGDN_FILESYSPATH, error);
    // WPD/MTP 的解析名称不保证可反向解析（实机存储区会返回 E_INVALIDARG）。
    // 跨后台调用保留完整 PIDL，名称只用于界面显示。
    TaskMemory pidl;
    result = SHGetIDListFromObject(item, reinterpret_cast<PIDLIST_ABSOLUTE *>(&pidl.ptr));
    const auto size = SUCCEEDED(result) ? ILGetSize(static_cast<PCIDLIST_ABSOLUTE>(pidl.ptr)) : 0;
    if (SUCCEEDED(result) && (size < 2 || size > maxPidlBytes)) result = E_INVALIDARG;
    if (error) *error = result;
    return SUCCEEDED(result) ? pidlPrefix + QString::fromLatin1(QByteArray(
        static_cast<const char *>(pidl.ptr), static_cast<qsizetype>(size)).toBase64()) : QString();
}
HRESULT bindFolder(IShellItem *item, ComPtr<IShellFolder> &folder)
{
    return item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(folder.put()));
}
// System.Size 的公共 PROPERTYKEY；当前 MinGW 的 propkey.h 未定义此符号。
const PROPERTYKEY sizeKey = {{0xb725f130, 0x47ef, 0x101a, {0xa5, 0xf1, 0x02, 0x60, 0x8c, 0x9e, 0xeb, 0xac}}, 12};
PortableFiles::Result enumerate(IShellItem *item, bool portableOnly)
{
    PortableFiles::Result output;
    ComPtr<IShellFolder> folder;
    HRESULT result = bindFolder(item, folder);
    if (FAILED(result)) {
        output.error = failure(QStringLiteral("无法打开 Shell 目录"), result);
        return output;
    }
    ComPtr<IEnumIDList> iterator;
    result = folder->EnumObjects(nullptr, static_cast<SHCONTF>(SHCONTF_FOLDERS | SHCONTF_NONFOLDERS
        | SHCONTF_INCLUDEHIDDEN), iterator.put());
    if (result == S_FALSE) return output;
    if (FAILED(result) || !iterator.get()) {
        output.error = failure(QStringLiteral("无法读取 Shell 目录"), FAILED(result) ? result : E_UNEXPECTED);
        return output;
    }
    for (;;) {
        TaskMemory childId;
        result = iterator->Next(1, reinterpret_cast<PITEMID_CHILD *>(&childId.ptr), nullptr);
        if (result == S_FALSE) break;
        if (FAILED(result) || !childId.ptr) {
            output.error = failure(QStringLiteral("Shell 目录枚举中断"), FAILED(result) ? result : E_UNEXPECTED);
            break;
        }
        ComPtr<IShellItem> child;
        result = SHCreateItemWithParent(nullptr, folder.get(), static_cast<PCUITEMID_CHILD>(childId.ptr),
                                       IID_PPV_ARGS(child.put()));
        if (FAILED(result)) {
            output.error = failure(QStringLiteral("无法读取 Shell 项目"), result);
            break;
        }
        SFGAOF attributes = 0;
        result = child->GetAttributes(SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_STREAM | SFGAO_CANDELETE
            | SFGAO_STORAGE | SFGAO_STORAGEANCESTOR, &attributes);
        if (FAILED(result)) {
            output.error = failure(QStringLiteral("无法读取 Shell 项目属性"), result);
            break;
        }
        // “此电脑”还可能包含控制面板等虚拟入口，只接受有存储能力的非文件系统目录。
        if (portableOnly && (!(attributes & SFGAO_FOLDER) || (attributes & SFGAO_FILESYSTEM)
            || (attributes & SFGAO_STREAM) || !(attributes & (SFGAO_STORAGE | SFGAO_STORAGEANCESTOR))))
            continue;
        const QString id = idOf(child.get(), &result);
        if (FAILED(result) || id.isEmpty()) {
            output.error = failure(QStringLiteral("无法取得 Shell 项目标识"), FAILED(result) ? result : E_UNEXPECTED);
            break;
        }
        const QString name = nameOf(child.get(), SIGDN_NORMALDISPLAY, &result);
        if (FAILED(result)) {
            output.error = failure(QStringLiteral("无法取得 Shell 项目名称"), result);
            break;
        }
        const bool directory = (attributes & SFGAO_FOLDER) && !(attributes & SFGAO_STREAM);
        qint64 size = -1;
        if (!directory) {
            ComPtr<IShellItem2> properties;
            ULONGLONG bytes = 0;
            if (SUCCEEDED(child->QueryInterface(IID_PPV_ARGS(properties.put())))
                && SUCCEEDED(properties->GetUInt64(sizeKey, &bytes))
                && bytes <= static_cast<ULONGLONG>(std::numeric_limits<qint64>::max()))
                size = static_cast<qint64>(bytes);
        }
        output.entries.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("id"), id},
            {QStringLiteral("directory"), directory}, {QStringLiteral("size"), size},
            {QStringLiteral("deletable"), bool(attributes & SFGAO_CANDELETE)}});
    }
    // 不返回不完整列表，以免把掉线或权限失败误显示为空目录。
    if (!output.error.isEmpty()) output.entries.clear();
    std::sort(output.entries.begin(), output.entries.end(), [](const QVariant &a, const QVariant &b) {
        const auto left = a.toMap(), right = b.toMap();
        if (left.value(QStringLiteral("directory")).toBool() != right.value(QStringLiteral("directory")).toBool())
            return left.value(QStringLiteral("directory")).toBool();
        return QString::compare(left.value(QStringLiteral("name")).toString(),
                                right.value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
    });
    return output;
}
// 使用目标命名空间自己的 ParseDisplayName，不能用 QFile::exists 判断 MTP 对象。
HRESULT noCollision(IShellItem *destination, const QString &name)
{
    ComPtr<IShellFolder> folder;
    HRESULT result = bindFolder(destination, folder);
    if (FAILED(result)) return result;
    auto native = name.toStdWString();
    TaskMemory existing;
    result = folder->ParseDisplayName(nullptr, nullptr, native.data(), nullptr,
        reinterpret_cast<PIDLIST_RELATIVE *>(&existing.ptr), nullptr);
    if (SUCCEEDED(result)) return HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
    // 某些便携驱动使用泛化失败表示不存在。再次枚举，只有完整枚举成功且无同名才可复制。
    const auto listing = enumerate(destination, false);
    if (!listing.error.isEmpty()) return FAILED(result) ? result : E_ACCESSDENIED;
    for (const auto &value : listing.entries) {
        if (QString::compare(value.toMap().value(QStringLiteral("name")).toString(), name, Qt::CaseInsensitive) == 0)
            return HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
    }
    return S_OK;
}
class CopySink final : public IFileOperationProgressSink {
public:
    CopySink(std::atomic_bool *cancelFlag, std::function<void(int)> callback, QString expectedName, bool deleteMode = false)
        : cancelled(cancelFlag), progress(std::move(callback)), expected(std::move(expectedName)), deleting(deleteMode) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(id, IID_IUnknown) || IsEqualIID(id, IID_IFileOperationProgressSink)) {
            *out = static_cast<IFileOperationProgressSink *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG count = --references; if (!count) delete this; return count; }
    HRESULT check() const { return cancelled && cancelled->load() ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : S_OK; }
    HRESULT notify(int value)
    {
        if (progress) {
            try { progress(value); }
            catch (...) { callbackFailed = true; return E_ABORT; }
        }
        return check();
    }
    HRESULT STDMETHODCALLTYPE StartOperations() override { return notify(0); }
    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT result) override { finish = result; return S_OK; }
    HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD flags, IShellItem *, IShellItem *destination, LPCWSTR newName) override
    {
        HRESULT result = check();
        if (FAILED(result)) return result;
        if (deleting) return E_ACCESSDENIED;
        if ((flags & TSF_OVERWRITE_EXIST) || (newName && QString::fromWCharArray(newName) != expected)) {
            itemResult = HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
            return itemResult;
        }
        itemResult = noCollision(destination, expected);
        return itemResult;
    }
    HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR,
                                            HRESULT result, IShellItem *created) override
    {
        itemResult = result;
        // Shell 的“跳过/已存在/改名解决冲突”也是非负状态，必须明确拒绝当作成功。
        if (result != S_OK || !created) return S_OK;
        const QString createdName = nameOf(created, SIGDN_PARENTRELATIVEPARSING);
        if (createdName != expected) {
            itemResult = HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
            return S_OK;
        }
        copied = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT total, UINT current) override
    {
        return notify(total ? static_cast<int>(std::min<quint64>(99, quint64(current) * 100 / total)) : 0);
    }
    HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem *, LPCWSTR) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem *, LPCWSTR, HRESULT, IShellItem *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR, HRESULT, IShellItem *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD, IShellItem *) override { return deleting ? check() : E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem *, HRESULT result, IShellItem *) override
    { itemResult=result;copied=(result==S_OK || result==COPYENGINE_S_DONT_PROCESS_CHILDREN || result==COPYENGINE_S_ALREADY_DONE);return S_OK; }
    HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem *, LPCWSTR) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem *, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResetTimer() override { return check(); }
    HRESULT STDMETHODCALLTYPE PauseTimer() override { return check(); }
    HRESULT STDMETHODCALLTYPE ResumeTimer() override { return check(); }
    bool copied = false;
    bool callbackFailed = false;
    HRESULT itemResult = E_PENDING;
    HRESULT finish = E_PENDING;
private:
    std::atomic<ULONG> references{1};
    std::atomic_bool *cancelled;
    std::function<void(int)> progress;
    QString expected;
    bool deleting;
};
}
#endif

namespace PortableFiles {
QString remove(const QString &sourceId, std::atomic_bool *cancelled)
{
#ifdef Q_OS_WIN
    if(cancelled && cancelled->load())return QStringLiteral("删除已取消");
    if(!validId(sourceId))return QStringLiteral("删除目标必须使用有效Shell绝对标识");
    Apartment apartment;if(FAILED(apartment.result))return apartmentError(apartment.result);
    ComPtr<IShellItem> source;HRESULT result=makeItem(sourceId,source);
    if(FAILED(result))return failure(QStringLiteral("无法找到待删除项目"),result);
    SFGAOF attributes=0;result=source->GetAttributes(SFGAO_CANDELETE,&attributes);
    if(FAILED(result) || !(attributes & SFGAO_CANDELETE))return QStringLiteral("设备或存储根目录不允许删除，或当前用户没有删除权限");
    ComPtr<IShellItem> parent;result=source->GetParent(parent.put());
    if(FAILED(result))return QStringLiteral("不能删除没有父目录的Shell根项目");
    const auto originalName=nameOf(source.get(),SIGDN_NORMALDISPLAY,&result);
    if(FAILED(result) || originalName.isEmpty())return QStringLiteral("无法确认待删除项目名称");
    ComPtr<IFileOperation> operation;
    result=CoCreateInstance(CLSID_FileOperation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(operation.put()));
    if(FAILED(result))return failure(QStringLiteral("无法创建后台删除操作"),result);
    result=operation->SetOperationFlags(FOF_NO_UI | FOFX_EARLYFAILURE | FOFX_NOCOPYHOOKS);
    if(FAILED(result))return failure(QStringLiteral("无法设置无界面删除模式"),result);
    ComPtr<CopySink> sink;*sink.put()=new CopySink(cancelled,{},QString(),true);
    DWORD cookie=0;result=operation->Advise(sink.get(),&cookie);
    if(FAILED(result))return failure(QStringLiteral("无法订阅删除结果"),result);
    result=operation->DeleteItem(source.get(),nullptr);
    if(SUCCEEDED(result))result=operation->PerformOperations();
    operation->Unadvise(cookie);
    BOOL aborted=FALSE;const auto abortedResult=operation->GetAnyOperationsAborted(&aborted);
    if(cancelled && cancelled->load())return QStringLiteral("删除已取消；已经删除的项目不能撤回，请刷新目录检查");
    if(FAILED(result))return failure(QStringLiteral("删除失败，目录可能已部分删除，请刷新检查"),result);
    if(FAILED(abortedResult) || aborted || !sink->copied || FAILED(sink->finish))
        return failure(QStringLiteral("删除未完整完成或被拒绝，请刷新目录检查"),sink->itemResult!=S_OK?sink->itemResult:E_ABORT);
    const auto remaining=enumerate(parent.get(),false);
    if(!remaining.error.isEmpty())return QStringLiteral("删除操作已返回，但无法重新读取父目录确认结果：")+remaining.error;
    for(const auto &value:remaining.entries)if(value.toMap().value("name").toString()==originalName)
        return QStringLiteral("设备仍返回同名项目，未确认删除成功，请刷新检查");
    return {};
#else
    Q_UNUSED(sourceId)
    Q_UNUSED(cancelled)
    return QStringLiteral("便携设备文件接口仅支持Windows");
#endif
}
Result roots()
{
#ifdef Q_OS_WIN
    Apartment apartment;
    if (FAILED(apartment.result)) return {{}, apartmentError(apartment.result)};
    TaskMemory computer;
    HRESULT result = SHGetKnownFolderIDList(FOLDERID_ComputerFolder, KF_FLAG_DEFAULT, nullptr,
                                           reinterpret_cast<PIDLIST_ABSOLUTE *>(&computer.ptr));
    ComPtr<IShellItem> item;
    if (SUCCEEDED(result))
        result = SHCreateItemFromIDList(static_cast<PCIDLIST_ABSOLUTE>(computer.ptr), IID_PPV_ARGS(item.put()));
    if (FAILED(result)) return {{}, failure(QStringLiteral("无法读取此电脑的便携设备"), result)};
    return enumerate(item.get(), true);
#else
    return {{}, QStringLiteral("便携设备文件接口仅支持 Windows")};
#endif
}
Result list(const QString &id)
{
#ifdef Q_OS_WIN
    if (!validId(id)) return {{}, QStringLiteral("Shell 目录标识必须是绝对路径，且不能含换行或空字符")};
    Apartment apartment;
    if (FAILED(apartment.result)) return {{}, apartmentError(apartment.result)};
    ComPtr<IShellItem> item;
    HRESULT result = makeItem(id, item);
    if (FAILED(result)) return {{}, failure(QStringLiteral("无法找到 Shell 目录，设备可能已断开或尚未授权"), result)};
    SFGAOF attributes = 0;
    result = item->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attributes);
    if (FAILED(result)) return {{}, failure(QStringLiteral("无法读取 Shell 目录属性"), result)};
    if (!(attributes & SFGAO_FOLDER) || (attributes & SFGAO_STREAM))
        return {{}, QStringLiteral("选中的 Shell 项目不是目录")};
    return enumerate(item.get(), false);
#else
    Q_UNUSED(id)
    return {{}, QStringLiteral("便携设备文件接口仅支持 Windows")};
#endif
}
QString copy(const QString &sourceId, const QString &destinationFolderId,
             std::atomic_bool *cancelled, std::function<void(int)> progress)
{
#ifdef Q_OS_WIN
    if (cancelled && cancelled->load()) return QStringLiteral("文件复制已取消");
    if (!validId(sourceId) || !validId(destinationFolderId)) return QStringLiteral("源文件和目标目录必须使用 Shell 绝对路径");
    Apartment apartment;
    if (FAILED(apartment.result)) return apartmentError(apartment.result);
    ComPtr<IShellItem> source, destination;
    HRESULT result = makeItem(sourceId, source);
    if (FAILED(result)) return failure(QStringLiteral("无法找到源文件，设备可能已断开或尚未授权"), result);
    result = makeItem(destinationFolderId, destination);
    if (FAILED(result)) return failure(QStringLiteral("无法找到目标目录"), result);
    SFGAOF attributes = 0;
    result = source->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attributes);
    if (FAILED(result)) return failure(QStringLiteral("无法读取源文件属性"), result);
    if ((attributes & SFGAO_FOLDER) && !(attributes & SFGAO_STREAM)) return QStringLiteral("便携设备传输只支持单个文件，请选择文件");
    result = destination->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attributes);
    if (FAILED(result)) return failure(QStringLiteral("无法读取目标目录属性"), result);
    if (!(attributes & SFGAO_FOLDER) || (attributes & SFGAO_STREAM)) return QStringLiteral("传输目标不是目录");
    const QString name = nameOf(source.get(), SIGDN_PARENTRELATIVEPARSING, &result);
    if (FAILED(result) || name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return failure(QStringLiteral("无法取得源文件名称"), FAILED(result) ? result : E_INVALIDARG);
    result = noCollision(destination.get(), name);
    if (FAILED(result)) return failure(QStringLiteral("目标已存在同名项目或无法确认目标目录，拒绝覆盖"), result);
    ComPtr<IFileOperation> operation;
    result = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(operation.put()));
    if (FAILED(result)) return failure(QStringLiteral("无法创建后台 Shell 文件操作"), result);
    // RENAMEONCOLLISION 防止检查后的竞争覆盖；发生自动改名时 sink 会报告失败，绝不删除该文件。
    result = operation->SetOperationFlags(FOF_NO_UI | FOF_RENAMEONCOLLISION | FOF_NOCOPYSECURITYATTRIBS
        | FOFX_EARLYFAILURE | FOFX_NOCOPYHOOKS);
    if (FAILED(result)) return failure(QStringLiteral("无法设置无界面传输模式"), result);
    ComPtr<CopySink> sink;
    *sink.put() = new CopySink(cancelled, std::move(progress), name);
    struct Subscription {
        IFileOperation *operation;
        DWORD cookie = 0;
        bool active = false;
        ~Subscription() { if (active) operation->Unadvise(cookie); }
    } subscription{operation.get()};
    // 每项 CopyItem sink 不接收 Start/Finish/UpdateProgress；全局订阅才包含完整生命周期。
    result = operation->Advise(sink.get(), &subscription.cookie);
    if (FAILED(result)) return failure(QStringLiteral("无法订阅后台文件传输进度"), result);
    subscription.active = true;
    const auto nativeName = name.toStdWString();
    result = operation->CopyItem(source.get(), destination.get(), nativeName.c_str(), nullptr);
    if (FAILED(result)) return failure(QStringLiteral("无法排队复制文件"), result);
    result = operation->PerformOperations();
    BOOL aborted = FALSE;
    const HRESULT abortedResult = operation->GetAnyOperationsAborted(&aborted);
    if ((cancelled && cancelled->load()) || result == HRESULT_FROM_WIN32(ERROR_CANCELLED)
        || sink->itemResult == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return QStringLiteral("文件复制已取消；目标可能留下部分文件，请刷新目录检查（未删除任何文件）");
    if (sink->callbackFailed) return QStringLiteral("文件进度回调失败，传输已中止；请刷新目标目录检查");
    if (FAILED(result)) return failure(QStringLiteral("Shell 文件复制失败；请刷新目标目录检查"), result);
    if (FAILED(abortedResult)) return failure(QStringLiteral("无法确认 Shell 复制完成状态"), abortedResult);
    if (aborted || !sink->copied || sink->itemResult != S_OK || FAILED(sink->finish))
        return failure(QStringLiteral("文件复制未完成、被跳过或遇到同名冲突；目标可能有部分文件，请刷新检查"),
                       sink->itemResult != S_OK ? sink->itemResult : (FAILED(sink->finish) ? sink->finish : E_ABORT));
    if (FAILED(sink->notify(100))) return QStringLiteral("文件已复制，但完成回调取消或失败；请刷新目标目录检查");
    return {};
#else
    Q_UNUSED(sourceId)
    Q_UNUSED(destinationFolderId)
    Q_UNUSED(cancelled)
    Q_UNUSED(progress)
    return QStringLiteral("便携设备文件接口仅支持 Windows");
#endif
}
}
