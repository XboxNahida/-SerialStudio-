#include "systemproxy.h"
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QTimer>
#include <QThread>
#include <memory>

namespace {
QString typeName(QNetworkProxy::ProxyType type)
{
    switch(type) {
    case QNetworkProxy::HttpProxy: return QStringLiteral("HTTP");
    case QNetworkProxy::HttpCachingProxy: return QStringLiteral("HTTP 缓存");
    case QNetworkProxy::Socks5Proxy: return QStringLiteral("SOCKS5");
    default: return QStringLiteral("系统默认");
    }
}
class SystemProxyFactory final : public QNetworkProxyFactory {
public:
    explicit SystemProxyFactory(SystemProxy::Query query):m_query(std::move(query)) {}
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query=QNetworkProxyQuery()) override
    { const auto result=m_query(query); return {result.isEmpty()?QNetworkProxy(QNetworkProxy::NoProxy):result.first()}; }
private:
    SystemProxy::Query m_query;
};
}

SystemProxy::SystemProxy(QObject *parent):SystemProxy([](const QNetworkProxyQuery &query) {
    return QNetworkProxyFactory::systemProxyForQuery(query);
},parent) {
    auto *timer=new QTimer(this);timer->setInterval(5000);
    connect(timer,&QTimer::timeout,this,&SystemProxy::refresh);timer->start();
    refresh();
}
SystemProxy::SystemProxy(Query query,QObject *parent):QObject(parent),m_query(std::move(query)) {}
QNetworkProxy SystemProxy::socketProxy(const QString &host,quint16 port) const
{
    const auto candidates=m_query(QNetworkProxyQuery(host,port,QStringLiteral("tcp"),QNetworkProxyQuery::TcpSocket));
    return candidates.isEmpty()?QNetworkProxy(QNetworkProxy::NoProxy):candidates.first();
}
QNetworkProxy SystemProxy::proxyFor(const QUrl &url) const
{
    const auto proxies=m_query(QNetworkProxyQuery(url));
    return proxies.isEmpty()?QNetworkProxy(QNetworkProxy::NoProxy):proxies.first();
}
QString SystemProxy::describe(const QUrl &url) const
{
    const auto proxy=proxyFor(url);
    if(proxy.type()==QNetworkProxy::NoProxy) return QStringLiteral("系统为此目标选择直连（VPN/TUN 遵循系统路由）");
    return QStringLiteral("系统代理 · %1 %2:%3").arg(typeName(proxy.type()),proxy.hostName()).arg(proxy.port());
}
void SystemProxy::updateSnapshot(const QNetworkProxy &http,const QNetworkProxy &https)
{
    m_configured=http.type()!=QNetworkProxy::NoProxy || https.type()!=QNetworkProxy::NoProxy;
    if(m_configured) {
        m_source=QStringLiteral("自动跟随系统代理");
        m_status=QStringLiteral("检测到示例目标使用代理；每个新连接按系统规则重新查询，保留绕过列表/PAC 决策");
    } else {
        m_source=QStringLiteral("自动跟随系统代理");
        m_status=QStringLiteral("当前示例目标为直连；其他目标仍按系统规则选择。VPN/TUN 遵循系统路由");
    }
}
void SystemProxy::refresh()
{
    if(m_refreshing)return;
    m_refreshing=true;
    // Windows WPAD/PAC 查询可能耗时数秒；状态轮询不占用 GUI 线程，慢查询不叠加。
    const auto result=std::make_shared<QPair<QNetworkProxy,QNetworkProxy>>();
    auto *worker=QThread::create([query=m_query,result] {
        const auto first=[&query](const char *url) {
            const auto candidates=query(QNetworkProxyQuery(QUrl(QString::fromLatin1(url))));
            return candidates.isEmpty()?QNetworkProxy(QNetworkProxy::NoProxy):candidates.first();
        };
        result->first=first("http://example.com");result->second=first("https://example.com");
    });
    connect(worker,&QThread::finished,this,[this,result] {
        m_refreshing=false;
        const auto oldSource=m_source,oldStatus=m_status;const bool oldConfigured=m_configured;
        updateSnapshot(result->first,result->second);
        if(oldSource!=m_source || oldStatus!=m_status || oldConfigured!=m_configured)emit changed();
    });
    // worker 不持有服务对象，服务销毁会自动断开回调，正在执行的系统查询可自行结束。
    connect(worker,&QThread::finished,worker,&QObject::deleteLater);
    worker->start();
}
void SystemProxy::apply(QNetworkAccessManager *manager) const
{ if(manager) manager->setProxyFactory(new SystemProxyFactory(m_query)); }
