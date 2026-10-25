#include "toolmanager.h"
#include "toolpaths.h"
#include "settingsstore.h"
#include "systemproxy.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

namespace {
bool trustedUrl(const QUrl &url) {
    return url.scheme()=="https" && url.userInfo().isEmpty() && url.port(443)==443
        && QStringList{"github.com","release-assets.githubusercontent.com","objects.githubusercontent.com","dl.google.com","www.python.org"}.contains(url.host());
}
bool noLinks(const QString &path) {
    for(QFileInfo p(path);!p.filePath().isEmpty();p=QFileInfo(p.absolutePath())) {
        if(p.isSymLink() || p.isJunction())return false;
        if(p.absoluteFilePath()==p.absolutePath())break;
    }
    return true;
}
bool writeFile(const QString &path,const QByteArray &data) {
    QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();
}
}
ToolManager::ToolManager(SettingsStore *settings,SystemProxy *proxy,const QString &root,QNetworkAccessManager *network,QObject *parent)
    :QObject(parent),m_settings(settings),m_root(root.isEmpty()?ToolPaths::root():root),m_access(network?network:&m_network) {
    Q_INIT_RESOURCE(tool_catalog);
    QFile catalog(":/tools/tools/toolchains.json");
    if(catalog.open(QIODevice::ReadOnly)) {
        const auto object=QJsonDocument::fromJson(catalog.readAll()).object();
        m_catalog=object.value("components").toArray().toVariantList()+object.value("optional").toArray().toVariantList();
    }
    if(settings)m_overrides=settings->value("tools/paths",QVariantMap()).toMap();
    ToolPaths::configure(m_root,m_overrides);
    if(proxy && !network)proxy->apply(m_access);
    m_deadline.setSingleShot(true);m_deadline.setInterval(10*60*1000);
    connect(&m_deadline,&QTimer::timeout,this,[this]{cancel();m_status=QStringLiteral("工具操作超时，已有版本保留；可稍后重试");emit changed();});
    connect(&m_extract,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit) {
        if(!m_staging)return;
        if(m_cancelled) {finish(QStringLiteral("已取消，已有工具保留"));return;}
        if(code!=0 || exit!=QProcess::NormalExit) {
            auto detail=QString::fromUtf8(m_extract.readAllStandardError()).trimmed();
            if(detail.isEmpty())detail=QString::fromUtf8(m_extract.readAllStandardOutput()).trimmed();
            if(detail.isEmpty())detail=QStringLiteral("进程未返回错误说明");
            finish(QStringLiteral("工具包已下载并校验，但解压失败（退出码 %1，程序：%2）：%3。请在高级路径选择有效 Python，或修复必备组件")
                .arg(code).arg(m_extract.program(),detail.left(800)));return;
        }
        const auto parentPath=QDir(m_root).filePath(m_active.value("id").toString());
        const auto target=QDir(parentPath).filePath(m_active.value("version").toString());
        if(!noLinks(parentPath) || !QDir().mkpath(parentPath) || QFileInfo::exists(target)
            || !QDir().rename(m_staging->filePath("output"),target)) {finish(QStringLiteral("无法提交工具目录或该版本已存在，已有文件保留"));return;}
        finish(QStringLiteral("工具已安装：%1；重连或新开终端后使用").arg(m_active.value("name").toString()));
    });
    connect(&m_extract,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error) {
        if(error==QProcess::FailedToStart)finish(QStringLiteral("无法启动 Python（%1）：%2。请修复必备工具或在高级路径指定有效解释器")
            .arg(m_extract.program(),m_extract.errorString()));
    });
}
ToolManager::~ToolManager() {
    m_deadline.stop();if(m_reply){m_reply->disconnect(this);m_reply->abort();}
    m_extract.disconnect(this);
    if(m_extract.state()!=QProcess::NotRunning){m_extract.kill();m_extract.waitForFinished(3000);}
    m_archive.close();
}
QString ToolManager::directory() const {return m_root;}
QVariantList ToolManager::components() const {
    QVariantList result;
    for(const auto &entry:m_catalog) {
        auto row=entry.toMap();QVariantList paths;bool available=true;
        const auto tools=row.value("tools").toMap();
        for(auto i=tools.cbegin();i!=tools.cend();++i) {
            const auto resolved=ToolPaths::resolve(i.key());available=available&&!resolved.isEmpty();
            paths.append(QVariantMap{{"name",i.key()},{"custom",m_overrides.value(i.key()).toString()},{"resolved",resolved}});
        }
        row.insert("paths",paths);row.insert("available",available);
        const auto installed=QDir(m_root).filePath(row.value("id").toString()+'/'+row.value("version").toString()+"/.serialstudio-files.json");
        row.insert("installed",QFileInfo::exists(installed));result.append(row);
    }
    return result;
}
QVariantList ToolManager::missingTools(const QStringList &names,const QVariantMap &explicitPaths) const {
    QVariantList missing;QSet<QString> seen;
    for(const auto &name:names) {
        if(seen.contains(name)||!ToolPaths::resolve(name,explicitPaths.value(name).toString()).isEmpty())continue;
        seen.insert(name);
        for(const auto &entry:m_catalog) {
            const auto row=entry.toMap();
            if(row.value("tools").toMap().contains(name)) {
                missing.append(QVariantMap{{"tool",name},{"componentId",row.value("id")},{"componentName",row.value("name")}});break;
            }
        }
    }
    return missing;
}
bool ToolManager::setPath(const QString &name,const QString &path) {
    bool known=false;for(const auto &item:m_catalog)if(item.toMap().value("tools").toMap().contains(name))known=true;
    if(!known || busy())return false;
    auto next=m_overrides;const auto url=QUrl(path.trimmed());
    const auto local=url.isLocalFile()?url.toLocalFile():path.trimmed();
    if(local.isEmpty())next.remove(name);else next.insert(name,local);
    if(m_settings && !m_settings->setValue("tools/paths",next)) {m_status=m_settings->error();emit changed();return false;}
    m_overrides=next;ToolPaths::configure(m_root,m_overrides);
    m_status=local.isEmpty()?QStringLiteral("已恢复自动查找；新连接生效"):
        ToolPaths::resolve(name).isEmpty()?QStringLiteral("指定路径不存在，连接时不会静默使用其他工具；请重新选择"):QStringLiteral("工具路径已保存；新连接生效");
    emit changed();return true;
}
void ToolManager::refresh() {emit changed();}
void ToolManager::openOfficial(const QString &id) {
    for(const auto &entry:m_catalog) {const auto item=entry.toMap();if(item.value("id")==id) {
        const QUrl url(item.value("homepage",item.value("licenseUrl")).toString());
        if(url.scheme()=="https")QDesktopServices::openUrl(url);
        return;
    }}
}
bool ToolManager::install(const QString &id) {
    if(busy())return false;
    QVariantMap component;for(const auto &entry:m_catalog)if(entry.toMap().value("id")==id)component=entry.toMap();
    if(component.isEmpty() || component.value("url").toString().isEmpty()) {m_status=QStringLiteral("此工具需通过官方许可页面自行安装，然后选择路径");emit changed();return false;}
    const QUrl url(component.value("url").toString());
    if(!trustedUrl(url) || component.value("sha256").toString().size()!=64) return false;
    const auto target=QDir(m_root).filePath(id+'/'+component.value("version").toString());
    if(QFileInfo::exists(target)) {m_status=QStringLiteral("该版本目录已存在，未覆盖；可指定其他工具路径");emit changed();return false;}
    if(ToolPaths::resolve("python").isEmpty()) {m_status=QStringLiteral("安全解压需要随包 Python，请重新安装必备组件或在高级设置指定 Python");emit changed();return false;}
    if(!noLinks(m_root) || !QDir().mkpath(m_root)) {m_status=QStringLiteral("工具目录不可写或包含链接，停止安装");emit changed();return false;}
    m_staging=std::make_unique<QTemporaryDir>(QDir(m_root).filePath(".install-XXXXXX"));
    if(!m_staging->isValid()) {finish(QStringLiteral("无法创建工具暂存目录"));return false;}
    m_active=component;m_received=0;m_progress=0;m_cancelled=false;m_exceeded=false;m_hash.reset();
    m_archive.setFileName(m_staging->filePath("archive.zip"));
    if(!m_archive.open(QIODevice::WriteOnly)) {finish(QStringLiteral("无法写入暂存文件"));return false;}
    QNetworkRequest request(url);request.setRawHeader("User-Agent","SerialStudio-Tools");request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::UserVerifiedRedirectPolicy);
    m_reply=m_access->get(request);auto *reply=m_reply.data();reply->setReadBufferSize(262144);
    connect(reply,&QNetworkReply::redirected,reply,[reply](const QUrl &next){if(trustedUrl(next))emit reply->redirectAllowed();else reply->abort();});
    connect(reply,&QIODevice::readyRead,this,[this,reply] {
        const auto data=reply->readAll();m_received+=data.size();
        if(m_received>m_active.value("bytes").toLongLong() || m_archive.write(data)!=data.size()) {m_exceeded=true;reply->abort();return;}
        m_hash.addData(data);m_progress=double(m_received)/double(qMax<qint64>(1,m_active.value("bytes").toLongLong()));emit changed();
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply] {
        m_reply=nullptr;reply->deleteLater();m_archive.close();
        if(m_cancelled || m_exceeded || reply->error()!=QNetworkReply::NoError || !trustedUrl(reply->url())
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200
            || m_received!=m_active.value("bytes").toLongLong()) {finish(m_cancelled?QStringLiteral("下载已取消，已有工具保留"):QStringLiteral("下载失败/大小异常；请检查系统代理或稍后重试，已有工具保留"));return;}
        if(QString::fromLatin1(m_hash.result().toHex()).compare(m_active.value("sha256").toString(),Qt::CaseInsensitive)!=0) {finish(QStringLiteral("SHA256 校验失败，拒绝安装"));return;}
        unpack();
    });
    m_deadline.start();m_status=QStringLiteral("正在按系统代理规则下载 %1").arg(component.value("name").toString());emit changed();return true;
}
void ToolManager::unpack() {
    QFile script(":/tools/tools/safe_archive.py");
    if(!script.open(QIODevice::ReadOnly) || !writeFile(m_staging->filePath("extract.py"),script.readAll())
        || !writeFile(m_staging->filePath("component.json"),QJsonDocument(QJsonObject::fromVariantMap(m_active)).toJson())) {finish(QStringLiteral("无法准备解压检查"));return;}
    m_status=QStringLiteral("摘要通过，正在安全解压…");emit changed();
    m_extract.start(ToolPaths::resolve("python"),{"-I",m_staging->filePath("extract.py"),m_archive.fileName(),m_staging->filePath("output"),m_staging->filePath("component.json")});
}
void ToolManager::finish(const QString &message) {
    m_deadline.stop();m_archive.close();m_staging.reset();m_status=message;emit changed();
}
void ToolManager::cancel() {
    m_cancelled=true;
    if(m_reply)m_reply->abort();
    else if(m_extract.state()!=QProcess::NotRunning)m_extract.kill();
}
bool ToolManager::seedManaged(const QString &source,const QString &root) {
    if(!noLinks(source) || !noLinks(root) || !QDir(source).exists() || !QDir().mkpath(root))return false;
    bool found=false;
    for(const auto &id:{"android-platform-tools","openssh","python","openocd","dfu-util"}) {
        const QDir component(QDir(source).filePath(id));
        if(!noLinks(component.path()))return false;
        for(const auto &version:component.entryList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks)) {
            if(!QRegularExpression("^[0-9][A-Za-z0-9.-]*$").match(version).hasMatch())return false;
            found=true;
            const QDir input(component.filePath(version));
            const auto output=QDir(root).filePath(QString(id)+'/'+version);
            if(!noLinks(output))return false;
            const bool existing=QFileInfo::exists(output);
            QJsonArray retained;
            if(existing) {
                QFile oldReceipt(output+"/.serialstudio-files.json");
                if(!noLinks(oldReceipt.fileName()) || !oldReceipt.open(QIODevice::ReadOnly) || oldReceipt.size()>8*1024*1024)return false;
                const auto oldDocument=QJsonDocument::fromJson(oldReceipt.readAll());
                if(!oldDocument.isArray() || oldDocument.array().size()>20000)return false;
                retained=oldDocument.array();
            }
            QFile receipt(input.filePath(".serialstudio-files.json"));
            if(!noLinks(receipt.fileName()) || !receipt.open(QIODevice::ReadOnly) || receipt.size()>8*1024*1024)return false;
            const auto bytes=receipt.readAll();const auto document=QJsonDocument::fromJson(bytes);
            if(!document.isArray() || document.array().isEmpty() || document.array().size()>20000)return false;
            QTemporaryDir staging(QDir(root).filePath(".seed-XXXXXX"));if(!staging.isValid())return false;
            for(const auto &entry:document.array()) {
                const auto item=entry.toObject();const auto relative=item.value("path").toString();
                if(relative.isEmpty() || relative==".serialstudio-files.json" || relative.contains('\\') || relative.contains(':') || QDir::isAbsolutePath(relative))return false;
                for(const auto &part:relative.split('/'))if(part.isEmpty() || part=="." || part==".." || part.endsWith('.') || part.endsWith(' '))return false;
                QFile file(input.filePath(relative));QCryptographicHash hash(QCryptographicHash::Sha256);
                if(!noLinks(file.fileName()) || !file.open(QIODevice::ReadOnly) || !hash.addData(&file)
                    || hash.result().toHex()!=item.value("sha256").toString().toLatin1().toLower())return false;
                file.close();const auto target=staging.filePath(relative);
                if(!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(file.fileName(),target))return false;
            }
            if(existing) {
                // 重装只补齐缺失文件，现存文件不覆盖，也不把用户文件纳入卸载清单。
                QSet<QString> tracked;
                for(const auto &entry:retained)tracked.insert(entry.toObject().value("path").toString().toCaseFolded());
                for(const auto &entry:document.array()) {
                    const auto relative=entry.toObject().value("path").toString();
                    const auto target=QDir(output).filePath(relative);
                    if(!noLinks(target))return false;
                    if(QFileInfo::exists(target))continue;
                    if(!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(staging.filePath(relative),target))return false;
                    if(!tracked.contains(relative.toCaseFolded())) {
                        retained.append(entry);tracked.insert(relative.toCaseFolded());
                    }
                }
                if(!writeFile(output+"/.serialstudio-files.json",QJsonDocument(retained).toJson()))return false;
            } else if(!writeFile(staging.filePath(".serialstudio-files.json"),bytes)
                || !QDir().mkpath(QFileInfo(output).absolutePath()) || !QDir().rename(staging.path(),output))return false;
        }
    }
    return found;
}
bool ToolManager::removeManaged(const QString &root,QString *report) {
    if(!noLinks(root))return false;
    int removed=0,kept=0;bool success=true;
    const QDir base(root);
    for(const auto &id:{"android-platform-tools","openssh","python","openocd","dfu-util","jlink","stm32cubeprogrammer"}) {
        const QDir component(base.filePath(id));
        if(!noLinks(component.absolutePath()))continue;
        for(const auto &version:component.entryList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks)) {
            if(!QRegularExpression("^[0-9][A-Za-z0-9.-]*$").match(version).hasMatch())continue;
            const QDir directory(component.filePath(version));const auto receiptPath=directory.filePath(".serialstudio-files.json");
            QFile receipt(receiptPath);
            if(!noLinks(receiptPath) || !receipt.open(QIODevice::ReadOnly) || receipt.size()>8*1024*1024)continue;
            const auto document=QJsonDocument::fromJson(receipt.readAll());receipt.close();
            if(!document.isArray())continue;
            QJsonArray remaining;
            for(const auto &entry:document.array()) {
                const auto item=entry.toObject();const auto path=item.value("path").toString();
                const auto expected=item.value("sha256").toString().toLatin1();
                bool safe=!path.isEmpty() && !QDir::isAbsolutePath(path) && !path.contains('\\') && !path.contains(':');
                for(const auto &part:path.split('/'))if(part.isEmpty() || part==".." || part=="." || part.endsWith('.') || part.endsWith(' '))safe=false;
                const auto absolute=directory.filePath(path);
                if(!safe || expected.size()!=64 || !noLinks(absolute)) {++kept;continue;}
                QFile file(absolute);
                if(!file.exists())continue;
                QCryptographicHash hash(QCryptographicHash::Sha256);
                if(!file.open(QIODevice::ReadOnly) || !hash.addData(&file) || hash.result().toHex()!=expected.toLower()) {remaining.append(entry);++kept;continue;}
                file.close();
                if(QFile::remove(absolute))++removed;else {remaining.append(entry);success=false;}
            }
            if(remaining.isEmpty())QFile::remove(receiptPath);
            else if(!writeFile(receiptPath,QJsonDocument(remaining).toJson()))success=false;
            QStringList directories;QDirIterator it(directory.path(),QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks,QDirIterator::Subdirectories);
            while(it.hasNext()){const auto p=it.next();if(noLinks(p))directories.append(p);}
            std::sort(directories.begin(),directories.end(),[](const QString &a,const QString &b){return a.size()>b.size();});
            for(const auto &p:directories)QDir().rmdir(p);
            QDir().rmdir(directory.path());
        }
        QDir().rmdir(component.path());
    }
    QDir().rmdir(root);
    if(report)*report=QStringLiteral("移除 %1 个受管文件；保留 %2 个改动/异常条目及未登记文件").arg(removed).arg(kept);
    return success;
}
