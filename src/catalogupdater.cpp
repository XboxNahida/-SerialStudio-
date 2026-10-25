#include "catalogupdater.h"
#include "systemproxy.h"
#include "updatecontroller.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <memory>

namespace {
const QString repository="https://github.com/XboxNahida/-SerialStudio-";
bool trusted(const QUrl &url) {
    return url.scheme()=="https"&&url.userInfo().isEmpty()&&url.port(443)==443
        &&QStringList{"api.github.com","github.com","release-assets.githubusercontent.com","objects.githubusercontent.com"}.contains(url.host());
}
}
CatalogUpdater::CatalogUpdater(CommandCatalog *catalog,QString appVersion,QNetworkAccessManager *network,QObject *parent)
    :QObject(parent),m_catalog(catalog),m_network(network?network:new QNetworkAccessManager(this)),m_appVersion(std::move(appVersion)) {
    if(!network){SystemProxy proxy;proxy.apply(m_network);}
    connect(catalog,&CommandCatalog::commandsChanged,this,[this]{
        if(UpdateController::compareVersions(m_latest,m_catalog->version())<=0)m_available=false;
        emit changed();
    });
}
void CatalogUpdater::fail(const QString &message){m_busy=false;m_status=message;emit changed();}
void CatalogUpdater::fetch(const QUrl &url,qint64 limit,std::function<void(QByteArray)> done) {
    if(!trusted(url)){fail(QStringLiteral("指令库下载来源不受信任"));return;}
    QNetworkRequest request(url);request.setRawHeader("User-Agent","SerialStudio-Catalog");
    request.setRawHeader("Accept","application/vnd.github+json");request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::UserVerifiedRedirectPolicy);
    auto *reply=m_network->get(request);
    connect(reply,&QNetworkReply::redirected,reply,[reply](const QUrl &redirect){if(trusted(redirect))emit reply->redirectAllowed();else reply->abort();});
    auto bytes=std::make_shared<QByteArray>();auto exceeded=std::make_shared<bool>(false);
    auto read=[reply,bytes,exceeded,limit]{
        if(*exceeded)return;
        bytes->append(reply->read(limit-bytes->size()+1));
        if(bytes->size()>limit){*exceeded=true;reply->abort();}
    };
    connect(reply,&QIODevice::readyRead,this,read);
    connect(reply,&QNetworkReply::downloadProgress,this,[this](qint64 got,qint64 total){m_progress=total>0?qBound(0.0,double(got)/double(total),1.0):0;emit changed();});
    auto *timer=new QTimer(reply);timer->setSingleShot(true);timer->start(60000);
    connect(timer,&QTimer::timeout,reply,&QNetworkReply::abort);
    connect(reply,&QNetworkReply::finished,this,[this,reply,bytes,exceeded,done=std::move(done),read]{
        read();reply->deleteLater();
        if(*exceeded||reply->error()!=QNetworkReply::NoError||reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200||!trusted(reply->url())){
            fail(QStringLiteral("指令库请求失败：网络、超时或响应大小异常，当前库保留"));return;
        }
        done(*bytes);
    });
}
void CatalogUpdater::check(bool manual) {
    if(m_busy)return;
    m_busy=true;m_progress=0;m_downloadUrl.clear();m_compatible=false;
    m_status=QStringLiteral("正在检查独立指令库更新…");emit changed();
    fetch(QUrl("https://api.github.com/repos/XboxNahida/-SerialStudio-/releases?per_page=100"),2*1024*1024,[this,manual](const QByteArray &bytes){
        QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);
        if(error.error!=QJsonParseError::NoError||!document.isArray()){fail(QStringLiteral("指令库发布列表格式错误"));return;}
        QJsonObject best;QString bestVersion;
        for(const auto &item:document.array()) {
            const auto object=item.toObject();const auto tag=object.value("tag_name").toString();
            if(!tag.startsWith("catalog-v")||object.value("draft").toBool())continue;
            const auto version=tag.mid(9);
            if(!CommandCatalog::validVersion(version)||object.value("html_url").toString()!=repository+"/releases/tag/"+tag)continue;
            if(!m_appVersion.contains('-')&&(object.value("prerelease").toBool()||version.contains('-')))continue;
            if(best.isEmpty()||UpdateController::compareVersions(version,bestVersion)>0){best=object;bestVersion=version;}
        }
        m_checked=QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        m_latest=bestVersion;m_available=false;
        if(best.isEmpty()) {m_busy=false;m_status=QStringLiteral("暂无独立指令库发布，继续使用当前库");emit changed();return;}
        const auto base=repository+"/releases/download/"+best.value("tag_name").toString()+"/";
        const auto manifestUrl=base+"terminal-catalog-manifest.json";bool found=false;
        for(const auto &item:best.value("assets").toArray()) {
            const auto asset=item.toObject();
            if(asset.value("name")=="terminal-catalog-manifest.json"&&asset.value("browser_download_url").toString()==manifestUrl)found=true;
        }
        if(!found){fail(QStringLiteral("指令库清单缺失或来源不匹配"));return;}
        fetch(QUrl(manifestUrl),16384,[this,base,bestVersion,manual](const QByteArray &data){
            QJsonParseError parse;const auto doc=QJsonDocument::fromJson(data,&parse);const auto object=doc.object();
            const auto name="terminal-catalog-v"+bestVersion+".json";
            const auto size=object.value("size").toDouble(-1);const auto hash=object.value("sha256").toString();
            const auto minimum=object.value("minAppVersion").toString();
            static const QRegularExpression digest("^[a-fA-F0-9]{64}$");
            if(parse.error!=QJsonParseError::NoError||!doc.isObject()||object.value("schemaVersion")!=QJsonValue(1)||object.value("catalogVersion").toString()!=bestVersion
                ||!CommandCatalog::validVersion(minimum)||object.value("fileName").toString()!=name||object.value("downloadUrl").toString()!=base+name
                ||size<=0||size>CommandCatalog::maximumBytes||size!=qint64(size)||!digest.match(hash).hasMatch()) {
                fail(QStringLiteral("指令库清单版本、摘要、大小或下载来源无效"));return;
            }
            m_minAppVersion=minimum;m_size=qint64(size);m_hash=hash.toLower();m_downloadUrl=base+name;
            m_compatible=UpdateController::compareVersions(minimum,m_appVersion)<=0;
            m_available=UpdateController::compareVersions(m_latest,m_catalog->version().isEmpty()?"0.0.0":m_catalog->version())>0
                &&(manual||QSettings().value("catalog/ignoredVersion").toString()!=m_latest);
            m_busy=false;m_progress=0;
            m_status=m_available?m_compatible?QStringLiteral("发现指令库 %1，可独立下载并应用").arg(m_latest):QStringLiteral("指令库 %1 需要 APP %2，请先升级 APP").arg(m_latest,minimum):QStringLiteral("当前指令库已是最新，或此版本已忽略");
            emit changed();
        });
    });
}
void CatalogUpdater::download() {
    if(m_busy||!canDownload())return;
    m_busy=true;m_progress=0;m_status=QStringLiteral("正在下载指令库并校验 SHA256…");emit changed();
    fetch(QUrl(m_downloadUrl),m_size,[this](const QByteArray &bytes){
        if(bytes.size()!=m_size||QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())!=m_hash){fail(QStringLiteral("指令库长度或 SHA256 不符，当前库保留"));return;}
        const auto object=QJsonDocument::fromJson(bytes).object();
        if(object.value("catalogVersion").toString()!=m_latest||object.value("minAppVersion").toString()!=m_minAppVersion){fail(QStringLiteral("指令库版本与发布清单不符，当前库保留"));return;}
        if(!m_catalog->apply(bytes)){fail(m_catalog->status());return;}
        m_busy=false;m_available=false;m_progress=1;m_status=QStringLiteral("指令库已更新至 %1；候选与说明已刷新").arg(m_latest);emit changed();
    });
}
void CatalogUpdater::ignore() {
    if(m_busy||m_latest.isEmpty())return;
    QSettings().setValue("catalog/ignoredVersion",m_latest);m_available=false;
    m_status=QStringLiteral("已忽略此指令库版本；手动检查可重新显示");emit changed();
}
