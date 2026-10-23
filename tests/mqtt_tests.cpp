#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QProcess>
#include <QSslSocket>
#include <QSslKey>
#include <QTemporaryDir>
#include <QStandardPaths>
#include "mqttclient.h"

namespace {
QByteArray field(const QByteArray &bytes) { return QByteArray(1,char(bytes.size()>>8))+QByteArray(1,char(bytes.size()))+bytes; }
QByteArray packet(quint8 header,const QByteArray &body) {
    QByteArray result(1,char(header)); int length=body.size();
    do { auto byte=quint8(length%128); length/=128; if(length)byte|=128; result+=char(byte); } while(length);
    return result+body;
}
class Broker : public QObject {
public:
    QTcpServer server;
    QTcpSocket *socket=nullptr;
    QByteArray input;
    QList<QByteArray> received;
    bool acknowledge=true,ping=true;
    Broker() {
        server.listen(QHostAddress::LocalHost);
        connect(&server,&QTcpServer::newConnection,this,[this] {
            socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,this,[this] {
                input+=socket->readAll();
                while(input.size()>=2) {
                    int length=0,multiplier=1,index=1; bool complete=false;
                    for(;index<input.size() && index<=4;++index) { const auto byte=quint8(input[index]); length+=(byte&127)*multiplier; multiplier*=128; if(!(byte&128)){++index;complete=true;break;} }
                    if(!complete || input.size()<index+length) return;
                    auto frame=input.left(index+length); input.remove(0,index+length); received.append(frame);
                    if(!acknowledge) continue;
                    const quint8 type=quint8(frame[0])>>4; const auto body=frame.mid(index);
                    if(type==1) socket->write(QByteArray::fromHex("20020000"));
                    if(type==8) socket->write(packet(0x90,body.left(2)+QByteArray(1,0)));
                    if(type==10) socket->write(packet(0xb0,body.left(2)));
                    if(type==3 && ((quint8(frame[0])>>1)&3)==1) { const int topicLength=quint8(body[0])*256+quint8(body[1]); socket->write(packet(0x40,body.mid(topicLength+2,2))); }
                    if(type==12 && ping) socket->write(QByteArray::fromHex("d000"));
                }
            });
        });
    }
    QVariantMap config() const { return {{"host","127.0.0.1"},{"port",server.serverPort()},{"clientId","local-test"},{"keepAlive",1}}; }
    bool hasType(int type) const { for(const auto &frame:received) if((quint8(frame[0])>>4)==type)return true; return false; }
};
class TlsBroker : public QTcpServer {
public:
    QSslCertificate certificate;
    QSslKey key;
    bool generated=false;
    QTemporaryDir directory;
    TlsBroker() {
        auto executable=QStandardPaths::findExecutable("openssl");
        if(executable.isEmpty() && QFile::exists("C:/msys64/ucrt64/bin/openssl.exe"))executable="C:/msys64/ucrt64/bin/openssl.exe";
        if(executable.isEmpty() || !QSslSocket::supportsSsl())return;
        QProcess process;
        process.start(executable,{"req","-x509","-newkey","rsa:2048","-keyout",directory.filePath("key.pem"),"-out",directory.filePath("cert.pem"),"-days","2","-nodes","-subj","/CN=localhost","-addext","subjectAltName=DNS:localhost"});
        if(!process.waitForFinished(10000) || process.exitCode()!=0)return;
        QFile cert(directory.filePath("cert.pem")),privateKey(directory.filePath("key.pem"));
        if(!cert.open(QIODevice::ReadOnly) || !privateKey.open(QIODevice::ReadOnly))return;
        certificate=QSslCertificate(cert.readAll()); key=QSslKey(privateKey.readAll(),QSsl::Rsa);
        generated=!certificate.isNull() && !key.isNull(); listen(QHostAddress::LocalHost);
    }
protected:
    void incomingConnection(qintptr descriptor) override {
        auto *socket=new QSslSocket(this); socket->setSocketDescriptor(descriptor);
        socket->setLocalCertificate(certificate); socket->setPrivateKey(key); socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        connect(socket,&QSslSocket::readyRead,this,[socket] { socket->readAll(); socket->write(QByteArray::fromHex("20020000")); });
        socket->startServerEncryption();
    }
};
}
class MqttTests : public QObject {
    Q_OBJECT
    static bool selectMessage(MqttClient &client,const QString &id,bool selected=true) {
        bool result=false;
        QMetaObject::invokeMethod(&client,"setEventSelected",Q_RETURN_ARG(bool,result),Q_ARG(QString,id),Q_ARG(bool,selected));
        return result;
    }
    static QString selectedText(MqttClient &client,bool hex=false,bool detailed=false) {
        QString result;
        QMetaObject::invokeMethod(&client,"selectedText",Q_RETURN_ARG(QString,result),Q_ARG(bool,hex),Q_ARG(bool,detailed),Q_ARG(bool,false));
        return result;
    }
private slots:
    void noncontiguousSelectionUsesArrivalOrderAndCurrentFormat() {
        Broker broker;MqttClient client;
        QVERIFY(client.connectBroker(broker.config()));QTRY_VERIFY(client.connected());
        broker.socket->write(packet(0x30,field("first")+QStringLiteral("第一条").toUtf8())+
            packet(0x30,field("middle")+"skip")+packet(0x33,field("last")+QByteArray::fromHex("0012")+"end"));
        QTRY_COMPARE(client.messages().size(),3);
        QVERIFY2(client.property("selectedCount").isValid(),"MQTT 尚未提供多选状态");
        const auto first=client.messages()[0].toMap(),last=client.messages()[2].toMap();
        QVERIFY(!first.value("eventId").toString().isEmpty());
        QVERIFY(selectMessage(client,last.value("eventId").toString()));
        QVERIFY(selectMessage(client,first.value("eventId").toString()));
        QCOMPARE(client.property("selectedCount").toInt(),2);
        QVERIFY(client.messages()[0].toMap().value("eventSelected").toBool());
        QVERIFY(!client.messages()[1].toMap().value("eventSelected").toBool());
        QCOMPARE(selectedText(client),QStringLiteral("first  第一条\nlast  end"));
        QCOMPARE(selectedText(client,true),QStringLiteral("first  e7acace4b880e69da1\nlast  656e64"));
        QCOMPARE(selectedText(client,false,true),first.value("timestamp").toString()+QStringLiteral(" · QoS 0\nfirst  第一条\n")+
            last.value("timestamp").toString()+QStringLiteral(" · QoS 1 · retain\nlast  end"));
        QVERIFY(QMetaObject::invokeMethod(&client,"selectAll"));QCOMPARE(client.property("selectedCount").toInt(),3);
        QVERIFY(QMetaObject::invokeMethod(&client,"clearSelection"));QCOMPARE(client.property("selectedCount").toInt(),0);
        QCOMPARE(selectedText(client),QString());
    }
    void selectionExpiresWithCountAndByteLimits() {
        Broker broker;MqttClient client;auto config=broker.config();config["keepAlive"]=0;
        QVERIFY(client.connectBroker(config));QTRY_VERIFY(client.connected());
        broker.socket->write(packet(0x30,field("topic")+"0"));QTRY_COMPARE(client.messages().size(),1);
        QVERIFY2(client.property("selectedCount").isValid(),"MQTT 尚未提供多选状态");
        const auto expired=client.messages().first().toMap().value("eventId").toString();
        QVERIFY(selectMessage(client,expired));
        QByteArray input;for(int i=1;i<=1000;++i)input+=packet(0x30,field("topic")+QByteArray::number(i));
        broker.socket->write(input);
        QTRY_COMPARE(client.messages().last().toMap().value("payload").toString(),QString("1000"));
        QCOMPARE(client.messages().size(),1000);QCOMPARE(client.property("selectedCount").toInt(),0);
        QVERIFY(!selectMessage(client,expired));
        QVERIFY(selectMessage(client,client.messages().last().toMap().value("eventId").toString()));
        const QByteArray large(5*1024*1024,'x');
        broker.socket->write(packet(0x30,field("large")+large));
        QTRY_COMPARE_WITH_TIMEOUT(client.messages().last().toMap().value("payload").toString().size(),large.size(),5000);
        QVERIFY(selectMessage(client,client.messages().last().toMap().value("eventId").toString()));
        broker.socket->write(packet(0x30,field("larger")+large));
        QTRY_COMPARE_WITH_TIMEOUT(client.messages().last().toMap().value("topic").toString(),QString("larger"),5000);
        QCOMPARE(client.messages().size(),1);QCOMPARE(client.property("selectedCount").toInt(),0);
        QVERIFY(!client.messages().first().toMap().value("eventSelected").toBool());
    }
    void reconnectKeepsLogSelectionAndClearResetsIdentity() {
        Broker broker;MqttClient client;
        QVERIFY(client.connectBroker(broker.config()));QTRY_VERIFY(client.connected());
        broker.socket->write(packet(0x30,field("saved")+"one"));QTRY_COMPARE(client.messages().size(),1);
        QVERIFY2(client.property("selectedCount").isValid(),"MQTT 尚未提供多选状态");
        const auto oldId=client.messages().first().toMap().value("eventId").toString();QVERIFY(selectMessage(client,oldId));
        client.disconnectBroker();QCOMPARE(client.property("selectedCount").toInt(),1);
        Broker second;QVERIFY(client.connectBroker(second.config()));QTRY_VERIFY(client.connected());
        second.socket->write(packet(0x30,field("new")+"two"));QTRY_COMPARE(client.messages().size(),2);
        QCOMPARE(selectedText(client),QString("saved  one"));
        QVERIFY(!client.messages().last().toMap().value("eventSelected").toBool());
        QSignalSpy reset(&client,SIGNAL(modelReset()));client.clear();QCOMPARE(reset.count(),1);
        QCOMPARE(client.property("selectedCount").toInt(),0);QCOMPARE(client.property("count").toInt(),0);
        second.socket->write(packet(0x30,field("after")+"three"));QTRY_COMPARE(client.messages().size(),1);
        QVERIFY(client.messages().first().toMap().value("eventId").toString()!=oldId);
        QVERIFY(!selectMessage(client,oldId));QVERIFY(!selectMessage(client,"missing"));
    }
    void handshakePublishSubscribe() {
        Broker broker; MqttClient client;
        QVERIFY(client.connectBroker(broker.config())); QTRY_VERIFY_WITH_TIMEOUT(client.connected(),2500);
        QVERIFY(broker.received[0].contains(QByteArray::fromHex("00044d51545404")));
        QVERIFY(client.subscribe("test/+",1)); QTRY_VERIFY(client.subscriptions().contains("test/+"));
        QVERIFY(client.publish("test/中文",QStringLiteral("消息"),1,true)); QTRY_VERIFY(!client.busy());
        QVERIFY(broker.hasType(3)); QVERIFY(client.unsubscribe("test/+")); QTRY_VERIFY(client.subscriptions().isEmpty());
        client.disconnectBroker(); QVERIFY(!client.connected());
    }
    void fragmentedInboundAndPuback() {
        Broker broker; MqttClient client; QSignalSpy spy(&client,&MqttClient::messageReceived);
        QVERIFY(client.connectBroker(broker.config())); QTRY_VERIFY(client.connected());
        const auto frame=packet(0x32,field("input")+QByteArray::fromHex("0011")+QByteArray("payload"));
        broker.socket->write(frame.left(1)); broker.socket->flush(); QTest::qWait(10); QCOMPARE(spy.count(),0);
        broker.socket->write(frame.mid(1,2)); broker.socket->flush(); QTest::qWait(10); QCOMPARE(spy.count(),0);
        broker.socket->write(frame.mid(3)+packet(0x30,field("other")+"next"));
        QTRY_COMPARE(spy.count(),2); QTRY_VERIFY(broker.hasType(4));
        QCOMPARE(client.messages().size(),2); QCOMPARE(spy[0][0].toString(),QString("input"));
        client.clear(); QVERIFY(client.messages().isEmpty());
    }
    void invalidInputAndMalformedPacket() {
        MqttClient invalid; QVERIFY(!invalid.connectBroker({{"host",""}}));
        QVERIFY(!invalid.publish("bad/#","x",0,false));
        Broker broker; MqttClient client; QVERIFY(client.connectBroker(broker.config())); QTRY_VERIFY(client.connected());
        QVERIFY(!client.publish("bad/#","x",0,false)); QVERIFY(!client.publish("valid","x",2,false));
        QVERIFY(!client.subscribe("bad/#/suffix",0));
        broker.socket->write(QByteArray::fromHex("30ffffffff01"));
        QTRY_VERIFY(!client.connected()); QVERIFY(client.status().contains(QStringLiteral("协议")));
    }
    void keepAliveAndDisconnect() {
        Broker broker; MqttClient client; QVERIFY(client.connectBroker(broker.config())); QTRY_VERIFY(client.connected());
        QTRY_VERIFY_WITH_TIMEOUT(broker.hasType(12),2500);
        broker.ping=false;
        QTRY_VERIFY_WITH_TIMEOUT(!client.connected(),4500);
        QVERIFY(client.status().contains(QStringLiteral("PINGRESP")));
    }
    void pendingLimitAndTimeout() {
        Broker broker; MqttClient client; auto config=broker.config(); config["keepAlive"]=0;
        QVERIFY(client.connectBroker(config)); QTRY_VERIFY(client.connected()); broker.acknowledge=false;
        for(int i=0;i<128;++i) QVERIFY(client.publish("out",QString::number(i),1,false));
        QVERIFY(!client.publish("out","over-limit",1,false));
        QTRY_VERIFY_WITH_TIMEOUT(!client.connected(),6500);
        QVERIFY(client.status().contains(QStringLiteral("确认超时"))); QVERIFY(!client.busy());
    }
    void badAcknowledgementAndMessageLimit() {
        Broker broker; MqttClient client; QVERIFY(client.connectBroker(broker.config())); QTRY_VERIFY(client.connected());
        QByteArray input;
        for(int i=0;i<1100;++i) input+=packet(0x30,field("topic")+QByteArray::number(i));
        broker.socket->write(input);
        QTRY_COMPARE_WITH_TIMEOUT(client.messages().size(),1000,3000);
        QTRY_COMPARE(client.messages().last().toMap().value("payload").toString(),QString("1099"));
        broker.socket->write(QByteArray::fromHex("40020033"));
        QTRY_VERIFY(!client.connected()); QVERIFY(client.status().contains(QStringLiteral("协议")));
    }
    void refusedConnectionAndSubscription() {
        Broker broker; broker.acknowledge=false; MqttClient client;
        QVERIFY(client.connectBroker(broker.config())); QTRY_VERIFY(broker.hasType(1));
        broker.socket->write(QByteArray::fromHex("20020005"));
        QTRY_VERIFY(!client.busy()); QVERIFY(!client.connected()); QVERIFY(client.status().contains(QStringLiteral("拒绝连接")));
        MqttClient other; Broker second;
        QVERIFY(other.connectBroker(second.config())); QTRY_VERIFY(other.connected());
        second.acknowledge=false; QVERIFY(other.subscribe("denied",1)); QTRY_VERIFY(second.hasType(8));
        const auto frame=second.received.last();
        second.socket->write(packet(0x90,frame.mid(2,2)+char(0x80)));
        QTRY_VERIFY(!other.busy()); QVERIFY(other.subscriptions().isEmpty());
        QVERIFY(other.status().contains(QStringLiteral("拒绝订阅")));
    }
    void tlsCertificateValidation() {
        TlsBroker broker; if(!broker.generated)QSKIP("本机缺少 OpenSSL 证书生成器或 TLS 后端");
        QVariantMap config{{"host","localhost"},{"port",broker.serverPort()},{"tls",true},{"clientId","tls-test"}};
        MqttClient client; QVERIFY(client.connectBroker(config));
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(),4000); QVERIFY(!client.connected());
        QVERIFY(client.status().contains("TLS"));
        config["caFile"]=broker.directory.filePath("cert.pem");
        QVERIFY(client.connectBroker(config)); QTRY_VERIFY_WITH_TIMEOUT(client.connected(),4000); client.disconnectBroker();
        config["host"]="127.0.0.1";
        QVERIFY(client.connectBroker(config)); QTRY_VERIFY_WITH_TIMEOUT(!client.busy(),4000); QVERIFY(!client.connected());
        QVERIFY(client.status().contains("TLS"));
    }
    void invalidConnackSessionFlag() {
        Broker broker; broker.acknowledge=false; MqttClient client;
        QVERIFY(client.connectBroker(broker.config())); QTRY_VERIFY(broker.hasType(1));
        broker.socket->write(QByteArray::fromHex("20020105"));
        QTRY_VERIFY(!client.busy()); QVERIFY(!client.connected());
        QVERIFY(client.status().contains(QStringLiteral("协议")));
    }
};
QTEST_GUILESS_MAIN(MqttTests)
#include "mqtt_tests.moc"
