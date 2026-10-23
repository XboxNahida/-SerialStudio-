#include "webdebugger.h"
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSslError>

namespace { constexpr qsizetype bodyLimit=8*1024*1024; }
WebDebugger::WebDebugger(QObject *parent):QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout,&QTimer::timeout,this,[this] {
        if(!m_reply) return;
        m_error=QStringLiteral("HTTP 请求超时"); m_reply->abort();
    });
}
bool WebDebugger::request(const QVariantMap &config)
{
    const auto reject=[this](const QString &message) { m_error=message; m_status=message; emit changed(); return false; };
    if(busy()) return reject(QStringLiteral("已有 HTTP 请求正在运行，请先取消"));
    const QUrl url(config.value("url").toString(),QUrl::StrictMode);
    if(!url.isValid() || url.host().isEmpty() || (url.scheme()!="http" && url.scheme()!="https"))
        return reject(QStringLiteral("URL 必须是有效的 http:// 或 https:// 地址"));
    const auto method=config.value("method","GET").toString().trimmed().toUpper().toLatin1();
    if(!QList<QByteArray>{"GET","POST","PUT","PATCH","DELETE","HEAD","OPTIONS"}.contains(method))
        return reject(QStringLiteral("HTTP 方法仅支持 GET/POST/PUT/PATCH/DELETE/HEAD/OPTIONS"));
    const QByteArray body=config.value("body").toString().toUtf8();
    if(body.size()>bodyLimit) return reject(QStringLiteral("请求体超过 8 MiB 上限"));
    bool timeoutOk=false; const int timeout=config.value("timeout",15000).toInt(&timeoutOk);
    if(!timeoutOk || timeout<1 || timeout>120000) return reject(QStringLiteral("超时必须为 1 至 120000 毫秒"));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    const auto headers=config.value("headers").toList();
    if(headers.size()>128) return reject(QStringLiteral("请求头最多 128 条"));
    const QRegularExpression token(QStringLiteral("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$"));
    qsizetype headerBytes=0;
    for(const auto &value:headers) {
        const auto header=value.toMap(); const auto name=header.value("name").toString().trimmed(),text=header.value("value").toString();
        if(name.isEmpty() && text.isEmpty()) continue;
        if(!token.match(name).hasMatch() || text.contains('\r') || text.contains('\n') || text.contains(QChar(0)))
            return reject(QStringLiteral("请求头名称无效或值含换行/空字符"));
        const auto bytes=text.toUtf8(); headerBytes+=name.size()+bytes.size();
        if(headerBytes>65536) return reject(QStringLiteral("请求头总长度超过 64 KiB"));
        if(name.compare("Content-Length",Qt::CaseInsensitive)==0 || name.compare("Transfer-Encoding",Qt::CaseInsensitive)==0)
            return reject(QStringLiteral("Content-Length 和 Transfer-Encoding 由 HTTP 客户端计算"));
        request.setRawHeader(name.toLatin1(),bytes);
    }
    const auto contentType=config.value("contentType").toString();
    if(contentType.contains('\r') || contentType.contains('\n') || contentType.contains(QChar(0)))
        return reject(QStringLiteral("Content-Type 不得含换行或空字符"));
    if(!contentType.isEmpty() && !request.hasRawHeader("Content-Type")) request.setHeader(QNetworkRequest::ContentTypeHeader,contentType);
    m_body.clear(); m_headers.clear(); m_error.clear(); m_code=0; m_elapsed=0; m_cancelled=false;
    m_clock.start(); m_status=QStringLiteral("正在发送 %1 %2").arg(QString::fromLatin1(method),url.toDisplayString(QUrl::RemovePassword));
    m_reply=method=="HEAD" ? m_manager.head(request) : m_manager.sendCustomRequest(request,method,body);
    m_reply->setReadBufferSize(65536);
    connect(m_reply,&QNetworkReply::readyRead,this,&WebDebugger::collect);
    connect(m_reply,&QNetworkReply::finished,this,&WebDebugger::finish);
    connect(m_reply,&QNetworkReply::sslErrors,this,[this](const QList<QSslError> &errors) {
        QStringList descriptions; for(const auto &error:errors) descriptions.append(error.errorString());
        m_error=QStringLiteral("TLS 证书校验失败：%1").arg(descriptions.join("；").left(1000));
        // 不调用 ignoreSslErrors，保持 Qt 的证书与主机名验证。
    });
    m_timeout.start(timeout); emit changed(); return true;
}
void WebDebugger::collect()
{
    if(!m_reply) return;
    const auto declared=m_reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    // HEAD 的 Content-Length 表示资源大小，响应本身没有正文。
    const bool hasResponseBody=m_reply->operation()!=QNetworkAccessManager::HeadOperation;
    if((hasResponseBody && declared>bodyLimit) || m_reply->bytesAvailable()>bodyLimit-m_body.size()) {
        m_error=QStringLiteral("响应体超过 8 MiB 上限，已停止读取"); m_reply->abort(); return;
    }
    while(m_reply && m_reply->bytesAvailable()>0) {
        const auto chunk=m_reply->read(qMin<qint64>(65536,bodyLimit-m_body.size()+1));
        if(chunk.isEmpty()) break;
        m_body+=chunk;
        if(m_body.size()>bodyLimit) { m_body.truncate(bodyLimit); m_error=QStringLiteral("响应体超过 8 MiB 上限，已停止读取"); m_reply->abort(); return; }
    }
}
void WebDebugger::finish()
{
    if(!m_reply) return;
    m_timeout.stop();
    auto *reply=m_reply.data();
    if(reply->isOpen()) collect();
    m_code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    m_headers.clear(); qsizetype bytes=0;
    for(const auto &header:reply->rawHeaderPairs()) {
        bytes+=header.first.size()+header.second.size();
        if(m_headers.size()>=256 || bytes>65536) { if(m_error.isEmpty())m_error=QStringLiteral("响应头超过显示上限（256 条 / 64 KiB）"); break; }
        m_headers.append(QVariantMap{{"name",QString::fromLatin1(header.first)},{"value",QString::fromUtf8(header.second)}});
    }
    if(m_error.isEmpty() && !m_cancelled && reply->error()!=QNetworkReply::NoError)
        m_error=reply->errorString();
    m_elapsed=m_clock.elapsed();
    m_status=m_cancelled?QStringLiteral("HTTP 请求已取消"):!m_error.isEmpty()?m_error:QStringLiteral("HTTP %1 · %2 ms · %3 字节").arg(m_code).arg(m_elapsed).arg(m_body.size());
    m_reply.clear(); reply->deleteLater(); emit changed();
}
void WebDebugger::cancel()
{
    if(!m_reply) return;
    m_cancelled=true; m_error.clear(); m_reply->abort();
}
