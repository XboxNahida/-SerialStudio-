#include "mqttclient.h"
#include <QAbstractSocket>
#include <QDateTime>
#include <QFile>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTcpSocket>

namespace {
constexpr qsizetype packetLimit=8*1024*1024;
quint16 read16(const QByteArray &bytes,qsizetype at=0) { return quint16(quint8(bytes[at])*256+quint8(bytes[at+1])); }
QByteArray word(quint16 value) { return QByteArray(1,char(value>>8))+QByteArray(1,char(value)); }
QByteArray stringField(const QString &value) { const auto bytes=value.toUtf8(); return word(quint16(bytes.size()))+bytes; }
bool validString(const QString &value,bool allowEmpty=false)
{
    if((!allowEmpty && value.isEmpty()) || value.toUtf8().size()>65535) return false;
    for(const auto point:value.toUcs4()) if(point==0 || (point>=0xd800 && point<=0xdfff) || (point&0xffff)==0xffff || (point&0xffff)==0xfffe) return false;
    return true;
}
bool validTopic(const QString &topic,bool filter)
{
    if(!validString(topic)) return false;
    if(!filter) return !topic.contains('#') && !topic.contains('+');
    const auto levels=topic.split('/');
    for(qsizetype i=0;i<levels.size();++i) {
        if(levels[i].contains('#') && (levels[i]!="#" || i!=levels.size()-1)) return false;
        if(levels[i].contains('+') && levels[i]!="+") return false;
    }
    return true;
}
}
MqttClient::MqttClient(QObject *parent):QObject(parent)
{
    m_timer.setInterval(200); m_clock.start();
    connect(&m_timer,&QTimer::timeout,this,&MqttClient::tick);
}
MqttClient::~MqttClient() { if(m_socket) { m_socket->disconnect(this); m_socket->abort(); } }
bool MqttClient::connectBroker(const QVariantMap &config)
{
    const auto reject=[this](const QString &error) { m_status=error; emit changed(); return false; };
    if(m_socket) return reject(QStringLiteral("已有 MQTT 会话，请先断开"));
    const auto host=config.value("host").toString().trimmed(),client=config.value("clientId").toString();
    bool portOk=false,keepOk=false;
    const int port=config.value("port",config.value("tls").toBool()?8883:1883).toInt(&portOk);
    const int keep=config.value("keepAlive",60).toInt(&keepOk);
    const auto user=config.value("user").toString(),password=config.value("password").toString();
    if(host.isEmpty() || host.size()>253 || host.contains('/') || !portOk || port<1 || port>65535 || !keepOk || keep<0 || keep>65535)
        return reject(QStringLiteral("MQTT 主机/端口/保活参数无效"));
    if(!validString(client) || !validString(user,true) || password.toUtf8().size()>65535 || (!password.isEmpty() && user.isEmpty()))
        return reject(QStringLiteral("MQTT client ID/用户名无效，密码需要用户名"));
    m_config=config; m_keepAlive=keep;
    if(config.value("tls").toBool()) {
        if(!QSslSocket::supportsSsl()) return reject(QStringLiteral("MQTT TLS 运行库不可用"));
        auto *socket=new QSslSocket(this); auto ssl=QSslConfiguration::defaultConfiguration();
        const auto caFile=config.value("caFile").toString();
        if(!caFile.isEmpty()) {
            QFile file(caFile);
            if(!file.open(QIODevice::ReadOnly) || file.size()>1048576) { delete socket; return reject(QStringLiteral("CA 文件不可读或超过 1 MiB")); }
            const auto certificates=QSslCertificate::fromData(file.readAll());
            if(certificates.isEmpty()) { delete socket; return reject(QStringLiteral("CA 文件中没有有效 PEM 证书")); }
            auto authorities=ssl.caCertificates(); authorities+=certificates; ssl.setCaCertificates(authorities);
        }
        ssl.setPeerVerifyMode(QSslSocket::VerifyPeer); socket->setSslConfiguration(ssl); m_socket=socket;
        connect(socket,&QSslSocket::encrypted,this,&MqttClient::sendConnect);
        connect(socket,&QSslSocket::sslErrors,this,[this](const QList<QSslError> &errors) {
            QStringList descriptions; for(const auto &error:errors)descriptions.append(error.errorString());
            close(QStringLiteral("MQTT TLS 证书校验失败：%1").arg(descriptions.join("；").left(1000)));
        });
    } else {
        m_socket=new QTcpSocket(this);
        connect(m_socket,&QAbstractSocket::connected,this,&MqttClient::sendConnect);
    }
    m_connected=false; m_connecting=true; m_waitPing=false; m_input.clear(); m_pending.clear(); m_subscriptions.clear();
    m_socket->setReadBufferSize(65536);
    connect(m_socket,&QAbstractSocket::readyRead,this,&MqttClient::receive);
    connect(m_socket,&QAbstractSocket::errorOccurred,this,[this](QAbstractSocket::SocketError) { if(m_socket)close(QStringLiteral("MQTT 连接错误：%1").arg(m_socket->errorString())); });
    connect(m_socket,&QAbstractSocket::disconnected,this,[this]{close(QStringLiteral("MQTT 对端已断开"));});
    m_connectStarted=m_clock.elapsed(); m_lastSent=m_connectStarted; m_timer.start(); m_status=QStringLiteral("正在连接 MQTT %1:%2").arg(host).arg(port); emit changed();
    if(auto *tls=qobject_cast<QSslSocket*>(m_socket.data())) tls->connectToHostEncrypted(host,quint16(port));
    else m_socket->connectToHost(host,quint16(port));
    return true;
}
void MqttClient::sendConnect()
{
    quint8 flags=m_config.value("cleanSession",true).toBool()?2:0;
    const auto user=m_config.value("user").toString(),password=m_config.value("password").toString();
    if(!user.isEmpty()) flags|=0x80;
    if(!password.isEmpty()) flags|=0x40;
    QByteArray body=stringField("MQTT")+char(4)+char(flags)+word(quint16(m_keepAlive))+stringField(m_config.value("clientId").toString());
    if(!user.isEmpty()) body+=stringField(user);
    if(!password.isEmpty()) body+=stringField(password);
    sendPacket(0x10,body);
}
bool MqttClient::sendPacket(quint8 header,const QByteArray &body)
{
    if(!m_socket || m_socket->state()!=QAbstractSocket::ConnectedState) return false;
    if(body.size()>packetLimit || m_socket->bytesToWrite()+body.size()+5>packetLimit) {
        m_status=QStringLiteral("MQTT 单包或待发送队列超过 8 MiB 上限"); emit changed(); return false;
    }
    QByteArray frame(1,char(header)); qsizetype length=body.size();
    do { quint8 byte=length%128; length/=128; if(length)byte|=128; frame+=char(byte); } while(length);
    frame+=body;
    if(m_socket->write(frame)!=frame.size()) { close(QStringLiteral("MQTT 写入失败")); return false; }
    m_lastSent=m_clock.elapsed(); return true;
}
void MqttClient::receive()
{
    if(!m_socket) return;
    m_input+=m_socket->read(qMin<qint64>(65536,packetLimit+5-m_input.size()));
    int handled=0;
    while(m_socket && m_input.size()>=2 && handled++<64) {
        quint32 length=0,multiplier=1; qsizetype index=1; bool complete=false;
        for(;index<m_input.size() && index<=4;++index) {
            const auto byte=quint8(m_input[index]); length+=(byte&127)*multiplier;
            if(!(byte&128)) { ++index; complete=true; break; } multiplier*=128;
        }
        if(!complete) { if(index>4)close(QStringLiteral("MQTT 协议错误：remaining length 编码超过四字节")); break; }
        if(length>packetLimit) { close(QStringLiteral("MQTT 协议错误：接收包超过 8 MiB")); return; }
        if(m_input.size()<index+length) break;
        const quint8 header=quint8(m_input[0]); const auto body=m_input.mid(index,length); m_input.remove(0,index+length);
        if(!processPacket(header,body)) { close(QStringLiteral("MQTT 协议错误：无效报文、确认 ID 或不支持的 QoS")); return; }
    }
    if(m_socket && (m_socket->bytesAvailable()>0 || handled>64)) QTimer::singleShot(0,this,&MqttClient::receive);
}
void MqttClient::close(const QString &status)
{
    auto *socket=m_socket.data(); m_socket.clear();
    if(socket) { socket->disconnect(this); socket->abort(); socket->deleteLater(); }
    m_config.remove("password"); m_pending.clear(); m_input.clear(); m_subscriptions.clear();
    m_connected=false; m_connecting=false; m_waitPing=false; m_timer.stop(); m_status=status; emit changed();
}
void MqttClient::disconnectBroker()
{
    if(m_connected && m_socket) { sendPacket(0xe0,{}); m_socket->flush(); }
    close(QStringLiteral("MQTT 已断开"));
}
bool MqttClient::processPacket(quint8 header,const QByteArray &body)
{
    if(header==0x20 && m_connecting && body.size()==2 && quint8(body[0])<=1 && quint8(body[1])<=5) {
        if(body[1]!=0 && body[0]!=0) return false;
        if(body[1]!=0) { close(QStringLiteral("MQTT 服务器拒绝连接，返回码 %1").arg(quint8(body[1]))); return true; }
        if(m_config.value("cleanSession",true).toBool() && body[0]!=0) return false;
        m_connected=true; m_connecting=false; m_status=QStringLiteral("MQTT 3.1.1 已连接（QoS 0/1）"); emit changed(); return true;
    }
    if(!m_connected) return false;
    const int type=header>>4,flags=header&15;
    if(type==3) {
        const int qos=(flags>>1)&3;
        if(qos>1 || body.size()<2 || (qos==0 && (flags&8))) return false;
        const auto topicLength=read16(body);
        const qsizetype offset=2+topicLength+(qos?2:0);
        if(topicLength==0 || body.size()<offset) return false;
        const auto topicBytes=body.mid(2,topicLength); const auto topic=QString::fromUtf8(topicBytes);
        if(topic.toUtf8()!=topicBytes || !validTopic(topic,false)) return false;
        if(qos==1) { const auto id=read16(body,2+topicLength); if(!id || !sendPacket(0x40,word(id)))return false; }
        const auto payload=body.mid(offset);
        m_messages.append(QVariantMap{{"topic",topic},{"payload",QString::fromUtf8(payload)},{"hex",QString::fromLatin1(payload.toHex())},
            {"qos",qos},{"retain",bool(flags&1)},{"duplicate",bool(flags&8)},{"timestamp",QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
            {"bytes",payload.size()+topicBytes.size()},{"eventId",QString::number(++m_eventId)},{"eventSelected",false}});
        m_messageBytes+=payload.size()+topicBytes.size();
        while(m_messages.size()>1000 || m_messageBytes>packetLimit) {
            const auto expired=m_messages.takeFirst().toMap();
            m_messageBytes-=expired.value("bytes").toLongLong();m_selected.remove(expired.value("eventId").toString());
        }
        emit messageReceived(topic,payload); emit changed(); return true;
    }
    if(flags!=0) return false;
    if(type==13) { if(!body.isEmpty() || !m_waitPing)return false; m_waitPing=false; return true; }
    if(type!=4 && type!=9 && type!=11) return false;
    if(body.size()!=(type==9?3:2)) return false;
    const auto id=read16(body);
    if(!id || !m_pending.contains(id) || m_pending[id].type!=type) return false;
    const auto pending=m_pending.take(id);
    if(type==9) {
        const auto code=quint8(body[2]);
        if(code!=0x80 && code>pending.qos) return false;
        if(code==0x80) m_status=QStringLiteral("MQTT 服务器拒绝订阅 %1").arg(pending.topic);
        else { if(!m_subscriptions.contains(pending.topic))m_subscriptions.append(pending.topic); m_status=QStringLiteral("MQTT 已订阅 %1，QoS %2").arg(pending.topic).arg(code); }
    } else if(type==11) { m_subscriptions.removeAll(pending.topic); m_status=QStringLiteral("MQTT 已退订 %1").arg(pending.topic); }
    else m_status=QStringLiteral("MQTT QoS 1 发布已确认");
    emit changed(); return true;
}
void MqttClient::tick()
{
    const auto now=m_clock.elapsed();
    if(m_connecting && now-m_connectStarted>10000) { close(QStringLiteral("MQTT 连接/CONNACK 超时")); return; }
    if(!m_connected) return;
    for(const auto &pending:m_pending) if(now-pending.sent>5000) { close(QStringLiteral("MQTT 确认超时，连接已关闭，未自动重发")); return; }
    if(m_waitPing && now-m_pingSent>qMax(1000,m_keepAlive*1000)) { close(QStringLiteral("MQTT PINGRESP 超时")); return; }
    if(m_keepAlive>0 && !m_waitPing && now-m_lastSent>=m_keepAlive*1000) {
        if(sendPacket(0xc0,{})) { m_waitPing=true; m_pingSent=now; }
    }
}
quint16 MqttClient::nextId()
{
    do { ++m_packetId; if(!m_packetId)++m_packetId; } while(m_pending.contains(m_packetId));
    return m_packetId;
}
bool MqttClient::publish(const QString &topic,const QString &text,int qos,bool retain)
{
    if(!m_connected || !validTopic(topic,false) || qos<0 || qos>1 || m_pending.size()>=128) {
        m_status=QStringLiteral("发布需要已连接、有效主题、QoS 0/1，且待确认数少于 128"); emit changed(); return false;
    }
    const auto payload=text.toUtf8();
    if(payload.size()+topic.toUtf8().size()+4>packetLimit) { m_status=QStringLiteral("MQTT 发布内容超过 8 MiB"); emit changed(); return false; }
    const auto id=qos?nextId():0;
    const auto body=stringField(topic)+(qos?word(id):QByteArray())+payload;
    if(!sendPacket(quint8(0x30|(qos<<1)|(retain?1:0)),body)) return false;
    if(qos)m_pending[id]={4,topic,m_clock.elapsed(),qos};
    m_status=qos?QStringLiteral("MQTT 等待 PUBACK"):QStringLiteral("MQTT QoS 0 已提交发送队列"); emit changed(); return true;
}
bool MqttClient::subscribe(const QString &topic,int qos)
{
    if(!m_connected || !validTopic(topic,true) || qos<0 || qos>1 || m_pending.size()>=128) {
        m_status=QStringLiteral("订阅需要已连接、有效主题过滤器、QoS 0/1，且待确认数少于 128"); emit changed(); return false;
    }
    const auto id=nextId();
    if(!sendPacket(0x82,word(id)+stringField(topic)+char(qos))) return false;
    m_pending[id]={9,topic,m_clock.elapsed(),qos}; m_status=QStringLiteral("MQTT 等待 SUBACK"); emit changed(); return true;
}
bool MqttClient::unsubscribe(const QString &topic)
{
    if(!m_connected || !validTopic(topic,true) || m_pending.size()>=128) {
        m_status=QStringLiteral("退订需要已连接、有效主题过滤器，且待确认数少于 128"); emit changed(); return false;
    }
    const auto id=nextId();
    if(!sendPacket(0xa2,word(id)+stringField(topic))) return false;
    m_pending[id]={11,topic,m_clock.elapsed(),0}; m_status=QStringLiteral("MQTT 等待 UNSUBACK"); emit changed(); return true;
}
void MqttClient::clear() { m_messages.clear();m_selected.clear();m_messageBytes=0;emit modelReset();emit changed(); }
bool MqttClient::setEventSelected(const QString &id,bool selected)
{
    for(auto &message:m_messages) {
        auto entry=message.toMap();
        if(entry.value("eventId").toString()!=id)continue;
        if(m_selected.contains(id)==selected)return true;
        entry["eventSelected"]=selected;message=entry;
        if(selected)m_selected.insert(id);else m_selected.remove(id);
        emit changed();return true;
    }
    return false;
}
void MqttClient::selectAll()
{
    if(m_selected.size()==m_messages.size())return;
    for(auto &message:m_messages) {
        auto entry=message.toMap();entry["eventSelected"]=true;message=entry;m_selected.insert(entry.value("eventId").toString());
    }
    emit changed();
}
void MqttClient::clearSelection()
{
    if(m_selected.isEmpty())return;
    m_selected.clear();
    for(auto &message:m_messages) {auto entry=message.toMap();entry["eventSelected"]=false;message=entry;}
    emit changed();
}
QString MqttClient::selectedText(bool hex,bool detailed,bool protocol) const
{
    Q_UNUSED(protocol)
    QStringList lines;
    // 断开和重连保留原日志及其选择；新消息只分配新 ID，clear() 才清除日志。
    for(const auto &message:m_messages) {
        const auto entry=message.toMap();
        if(!m_selected.contains(entry.value("eventId").toString()))continue;
        if(detailed)lines.append(entry.value("timestamp").toString()+QStringLiteral(" · QoS ")+entry.value("qos").toString()+
            (entry.value("retain").toBool()?QStringLiteral(" · retain"):QString()));
        lines.append(entry.value("topic").toString()+"  "+entry.value(hex?"hex":"payload").toString());
    }
    return lines.join('\n');
}
