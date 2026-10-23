#include "updatecontroller.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <array>
#include <memory>

namespace {
const QString repository=QStringLiteral("https://github.com/XboxNahida/-SerialStudio-");
constexpr qint64 maximumInstaller=256*1024*1024;
struct Version {std::array<quint64,5> parts{};bool valid=false;};
Version parseVersion(QString text) {
    if(text.startsWith('v'))text.remove(0,1);
    static const QRegularExpression pattern(QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-(alpha|beta|rc)\\.(0|[1-9][0-9]*))?$"));
    const auto match=pattern.match(text);Version result;
    if(!match.hasMatch())return result;
    for(int i=0;i<3;++i){bool ok=false;result.parts[size_t(i)]=match.captured(i+1).toULongLong(&ok);if(!ok)return result;}
    const auto channel=match.captured(4);
    result.parts[3]=channel.isEmpty()?3:channel=="rc"?2:channel=="beta"?1:0;
    if(!channel.isEmpty()){bool ok=false;result.parts[4]=match.captured(5).toULongLong(&ok);if(!ok)return result;}
    result.valid=true;return result;
}
bool safeReplyUrl(const QUrl &url) {
    if(url.scheme()!="https"||!url.userInfo().isEmpty()||url.port(443)!=443)return false;
    return url.host()=="api.github.com"||url.host()=="github.com"||url.host()=="release-assets.githubusercontent.com"||url.host()=="objects.githubusercontent.com";
}
QNetworkRequest requestFor(const QUrl &url) {
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent","SerialStudio-Updater");
    request.setRawHeader("Accept","application/vnd.github+json");
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::UserVerifiedRedirectPolicy);
    return request;
}
void guardRedirects(QNetworkReply *reply) {
    QObject::connect(reply,&QNetworkReply::redirected,reply,[reply](const QUrl &url){
        if(safeReplyUrl(url))emit reply->redirectAllowed();else reply->abort();
    });
}
bool successful(QNetworkReply *reply) {
    return reply->error()==QNetworkReply::NoError&&reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==200&&safeReplyUrl(reply->url());
}
QString uninstallPath(){return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("卸载.exe"));}
}

UpdateController::UpdateController(QString version,QNetworkAccessManager *network,QObject *parent)
    :QObject(parent),m_network(network?network:new QNetworkAccessManager(this)),m_current(std::move(version)),
      m_cacheRoot(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/updates") {
    m_launch=[](const QString &program,const QStringList &arguments){return QProcess::startDetached(program,arguments);};
}
int UpdateController::compareVersions(const QString &left,const QString &right) {
    const auto a=parseVersion(left),b=parseVersion(right);
    if(!a.valid||!b.valid)return 0;
    return a.parts==b.parts?0:a.parts>b.parts?1:-1;
}
void UpdateController::fail(const QString &message) {
    m_busy=false;m_ready=false;m_status=message;
    if(!m_downloadPath.isEmpty())QFile::remove(m_downloadPath);
    emit changed();
}
void UpdateController::fetch(const QUrl &url,qint64 limit,std::function<void(QByteArray)> done) {
    auto *reply=m_network->get(requestFor(url));guardRedirects(reply);
    auto bytes=std::make_shared<QByteArray>();auto exceeded=std::make_shared<bool>(false);
    connect(reply,&QIODevice::readyRead,this,[reply,bytes,exceeded,limit]{
        if(*exceeded)return;
        bytes->append(reply->read(limit-bytes->size()+1));
        if(bytes->size()>limit){*exceeded=true;reply->abort();}
    });
    auto *deadline=new QTimer(reply);deadline->setSingleShot(true);deadline->start(30000);
    connect(deadline,&QTimer::timeout,reply,&QNetworkReply::abort);
    connect(reply,&QNetworkReply::finished,this,[this,reply,bytes,exceeded,done=std::move(done)]{
        reply->deleteLater();
        if(*exceeded||!successful(reply)){fail(QStringLiteral("更新请求失败：网络、超时或响应大小异常，请重试"));return;}
        done(*bytes);
    });
}
void UpdateController::check(bool manual) {
    if(m_busy)return;
    pruneCache();
    m_busy=true;m_status=QStringLiteral("正在检查 GitHub 更新…");emit changed();
    fetch(QUrl("https://api.github.com/repos/XboxNahida/-SerialStudio-/releases?per_page=30"),1024*1024,[this,manual](const QByteArray &bytes){
        QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);
        if(error.error!=QJsonParseError::NoError||!document.isArray()){fail(QStringLiteral("更新响应格式无效"));return;}
        QJsonObject best;QString bestVersion;
        for(const auto &value:document.array()){
            const auto item=value.toObject();const auto tag=item.value("tag_name").toString();
            const auto version=tag.startsWith('v')?tag.mid(1):tag;
            if(item.value("draft").toBool()||!parseVersion(version).valid)continue;
            if(!m_current.contains('-')&&(item.value("prerelease").toBool()||version.contains('-')))continue;
            if(item.value("html_url").toString()!=repository+"/releases/tag/"+tag)continue;
            if(best.isEmpty()||compareVersions(version,bestVersion)>0){best=item;bestVersion=version;}
        }
        m_latest=bestVersion;m_tag=best.value("tag_name").toString();m_releaseUrl=best.value("html_url").toString();
        m_available=compareVersions(m_latest,m_current)>0&&(manual||QSettings().value("updates/ignoredTag").toString()!=m_tag);
        m_assetUrl.clear();m_hashUrl.clear();m_assetSize=0;
        m_assetName="SerialStudio-"+m_latest+"-Windows-x64-Setup.exe";
        const auto base=repository+"/releases/download/"+m_tag+"/";
        for(const auto &entry:best.value("assets").toArray()){
            const auto asset=entry.toObject();const auto name=asset.value("name").toString();
            const auto url=asset.value("browser_download_url").toString();
            if(url!=base+name)continue;
            if(name==m_assetName){m_assetUrl=url;m_assetSize=asset.value("size").toInteger();}
            if(name==m_assetName+".sha256")m_hashUrl=url;
        }
        if(m_assetSize<=0||m_assetSize>maximumInstaller)m_assetUrl.clear();
        if(!m_downloadPath.isEmpty())QFile::remove(m_downloadPath);
        m_ready=false;m_busy=false;m_progress=0;
        m_status=m_available?QStringLiteral("发现新版本 %1，可下载完整安装包").arg(m_latest):QStringLiteral("当前没有可用更新（%1渠道）").arg(m_current.contains('-')?"测试":"正式");
        emit changed();
    });
}
void UpdateController::pruneCache() {
    const QFileInfo root(m_cacheRoot);
    if(root.isSymLink()||root.isJunction())return;
    static const QRegularExpression directoryPattern("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    static const QRegularExpression filePattern("^SerialStudio-[0-9]+\\.[0-9]+\\.[0-9]+(?:-(?:alpha|beta|rc)\\.[0-9]+)?-Windows-x64-Setup\\.exe$");
    const auto cutoff=QDateTime::currentDateTimeUtc().addDays(-7);
    for(const auto &folder:QDir(m_cacheRoot).entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot)){
        if(folder.isSymLink()||folder.isJunction()||!directoryPattern.match(folder.fileName()).hasMatch())continue;
        QDir directory(folder.absoluteFilePath());
        for(const auto &file:directory.entryInfoList(QDir::Files)){
            if(!file.isSymLink()&&!file.isJunction()&&file.absoluteFilePath()!=m_downloadPath&&file.lastModified().toUTC()<cutoff&&filePattern.match(file.fileName()).hasMatch())
                QFile::remove(file.absoluteFilePath());
        }
        // rmdir 只移除空目录；其他文件和正在使用的安装包保留。
        QDir().rmdir(folder.absoluteFilePath());
    }
}
void UpdateController::ignore(){if(m_busy)return;QSettings().setValue("updates/ignoredTag",m_tag);m_available=false;m_status=QStringLiteral("已忽略此版本，手动检查可重新显示");emit changed();}
void UpdateController::download() {
    if(m_busy||!canDownload())return;
    if(!m_downloadPath.isEmpty())QFile::remove(m_downloadPath);
    m_ready=false;m_busy=true;m_progress=0;m_status=QStringLiteral("正在获取 SHA256 校验信息…");emit changed();
    fetch(QUrl(m_hashUrl),4096,[this](const QByteArray &bytes){
        const auto line=QString::fromUtf8(bytes).trimmed();
        const QRegularExpression pattern("^([A-Fa-f0-9]{64})[ \\t]+\\*?"+QRegularExpression::escape(m_assetName)+"$");
        const auto match=pattern.match(line);
        if(!match.hasMatch()){fail(QStringLiteral("SHA256 校验文件格式或文件名不符"));return;}
        m_expectedHash=match.captured(1).toLower();
        const auto folder=QDir(m_cacheRoot).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
        if(!QDir().mkpath(folder)){fail(QStringLiteral("无法创建更新缓存目录"));return;}
        m_downloadPath=QDir(folder).filePath(m_assetName);
        auto file=std::make_shared<QSaveFile>(m_downloadPath);
        if(!file->open(QIODevice::WriteOnly)){fail(QStringLiteral("无法写入更新缓存"));return;}
        auto *reply=m_network->get(requestFor(QUrl(m_assetUrl)));guardRedirects(reply);
        auto count=std::make_shared<qint64>(0);auto failed=std::make_shared<bool>(false);
        m_status=QStringLiteral("正在下载安装包…");emit changed();
        connect(reply,&QIODevice::readyRead,this,[this,reply,file,count,failed]{
            while(!*failed&&reply->bytesAvailable()>0){
                const auto block=reply->read(65536);*count+=block.size();
                if(*count>m_assetSize||file->write(block)!=block.size()){*failed=true;reply->abort();break;}
                m_progress=double(*count)/double(m_assetSize);
            }
            emit changed();
        });
        auto *deadline=new QTimer(reply);deadline->setSingleShot(true);deadline->start(5*60*1000);
        connect(deadline,&QTimer::timeout,reply,&QNetworkReply::abort);
        connect(reply,&QNetworkReply::finished,this,[this,reply,file,count,failed]{
            reply->deleteLater();
            if(*failed||!successful(reply)||*count!=m_assetSize){file->cancelWriting();fail(QStringLiteral("下载失败或安装包长度不符，请重试"));return;}
            if(!file->commit()||!verifyFile()){fail(QStringLiteral("安装包 SHA256 校验失败，已删除下载文件"));return;}
            m_busy=false;m_ready=true;m_progress=1;m_status=QStringLiteral("下载完成，SHA256 校验通过。点击安装后退出应用并打开安装向导。");emit changed();
        });
    });
}
bool UpdateController::verifyFile() const {
    QFile file(m_downloadPath);QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.size()==m_assetSize&&file.open(QIODevice::ReadOnly)&&hash.addData(&file)&&QString::fromLatin1(hash.result().toHex())==m_expectedHash;
}
void UpdateController::install() {
    if(m_busy||!m_ready)return;
    if(!verifyFile()){fail(QStringLiteral("安装前 SHA256 校验失败，请重新下载"));return;}
    if(!m_launch(m_downloadPath,{"/UPDATEPID="+QString::number(QCoreApplication::applicationPid())})){
        m_status=QStringLiteral("无法启动安装程序，请重试或打开 Release 下载");emit changed();return;
    }
    emit exitRequested();
}
bool UpdateController::uninstallAvailable() const{return QFileInfo(uninstallPath()).isFile();}
void UpdateController::uninstall() {
    if(m_busy||!uninstallAvailable())return;
    if(m_launch(uninstallPath(),{}))emit exitRequested();
    else {m_status=QStringLiteral("无法打开卸载程序");emit changed();}
}
void UpdateController::openRelease(){QDesktopServices::openUrl(QUrl(m_releaseUrl.isEmpty()?repository+"/releases":m_releaseUrl));}
