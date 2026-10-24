#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTest>
#include <QSignalSpy>
#include <QNetworkProxyFactory>
#include <QNetworkProxy>
#include <QThread>
#include <atomic>
#include "systemproxy.h"
#include "mqttclient.h"

class ProxyTests: public QObject {
    Q_OBJECT
private slots:
    void slowStatusRefreshDoesNotBlockOrPileUp() {
        std::atomic<int> calls{0};
        QElapsedTimer elapsed;elapsed.start();
        SystemProxy service([&](const QNetworkProxyQuery &) {
            ++calls;QThread::msleep(150);
            return QList<QNetworkProxy>{QNetworkProxy(QNetworkProxy::HttpProxy,"proxy.test",8080)};
        });
        QSignalSpy changed(&service,&SystemProxy::changed);
        service.refresh();service.refresh();service.refresh();
        QVERIFY2(elapsed.elapsed()<150,"慢系统查询阻塞了调用线程");
        bool heartbeat=false;QTimer::singleShot(0,this,[&]{heartbeat=true;});
        QTRY_VERIFY_WITH_TIMEOUT(heartbeat,100);
        QCOMPARE(changed.count(),0);
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(),1,3000);
        QCOMPARE(calls.load(),2);
        QVERIFY(service.configured());
    }
    void mqttActuallyUsesHttpConnectTunnel() {
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));QByteArray connectRequest,mqttPacket;bool tunnel=false;
        connect(&server,&QTcpServer::newConnection,this,[&] {
            auto *socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,socket,[&,socket] {
                const auto data=socket->readAll();
                if(!tunnel){connectRequest+=data;if(connectRequest.contains("\r\n\r\n")){tunnel=true;socket->write("HTTP/1.1 200 Connection established\r\n\r\n");}}
                else {mqttPacket+=data;if(mqttPacket.size()>=2)socket->write(QByteArray::fromHex("20020000"));}
            });
        });
        SystemProxy service([&](const QNetworkProxyQuery &) {return QList<QNetworkProxy>{QNetworkProxy(QNetworkProxy::HttpProxy,"127.0.0.1",server.serverPort())};});
        MqttClient client(&service);
        QVERIFY(client.connectBroker({{"host","broker.invalid"},{"port",1883},{"clientId","proxy-test"}}));
        QTRY_VERIFY_WITH_TIMEOUT(client.connected(),3000);
        QVERIFY(connectRequest.startsWith("CONNECT broker.invalid:1883 HTTP/1.1"));
        QVERIFY(!mqttPacket.isEmpty());QCOMPARE(quint8(mqttPacket[0]),quint8(0x10));
        client.disconnectBroker();
    }
    void neverAppendsDirectFallback() {
        SystemProxy service([](const QNetworkProxyQuery &) {return QList<QNetworkProxy>{QNetworkProxy(QNetworkProxy::HttpProxy,"proxy.invalid",8080),QNetworkProxy(QNetworkProxy::NoProxy)};});
        QNetworkAccessManager manager;service.apply(&manager);
        const auto routes=manager.proxyFactory()->queryProxy(QNetworkProxyQuery(QUrl("https://outside.test")));
        QCOMPARE(routes.size(),1);QCOMPARE(routes.first().type(),QNetworkProxy::HttpProxy);
    }
    void followsSystemOrderAndChanges() {
        QList<QNetworkProxy> candidates{QNetworkProxy(QNetworkProxy::NoProxy),QNetworkProxy(QNetworkProxy::HttpProxy,"proxy.test",8080)};
        SystemProxy service([&](const QNetworkProxyQuery &) { return candidates; });
        QCOMPARE(service.proxyFor(QUrl("https://outside.test")).type(),QNetworkProxy::NoProxy);
        candidates.removeFirst();
        QCOMPARE(service.proxyFor(QUrl("https://outside.test")).hostName(),QString("proxy.test"));
        candidates={QNetworkProxy(QNetworkProxy::Socks5Proxy,"socks.test",1080)};
        QCOMPARE(service.proxyFor(QUrl("https://outside.test")).type(),QNetworkProxy::Socks5Proxy);
    }
    void tcpUsesHostPortAndRejectsCachingProxy() {
        QNetworkProxyQuery observed;
        SystemProxy service([&](const QNetworkProxyQuery &q) { observed=q; return QList<QNetworkProxy>{QNetworkProxy(QNetworkProxy::HttpCachingProxy,"cache.test",8080)}; });
        const auto proxy=service.socketProxy("broker.test",8883);
        QCOMPARE(observed.queryType(),QNetworkProxyQuery::TcpSocket);
        QCOMPARE(observed.peerHostName(),QString("broker.test"));
        QCOMPARE(observed.peerPort(),8883);
        QVERIFY(!(proxy.capabilities() & QNetworkProxy::TunnelingCapability));
        QVERIFY(proxy.type()!=QNetworkProxy::NoProxy);
    }
    void httpActuallyReachesProxy() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); QByteArray request;
        connect(&server,&QTcpServer::newConnection,this,[&] {
            auto *socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,socket,[&,socket] {
                request+=socket->readAll();
                if(request.contains("\r\n\r\n")) { socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK"); socket->disconnectFromHost(); }
            });
        });
        SystemProxy service([&](const QNetworkProxyQuery &) { return QList<QNetworkProxy>{QNetworkProxy(QNetworkProxy::HttpProxy,"127.0.0.1",server.serverPort())}; });
        QNetworkAccessManager manager; service.apply(&manager);
        auto *reply=manager.get(QNetworkRequest(QUrl("http://never-resolve.invalid/probe")));
        QSignalSpy done(reply,&QNetworkReply::finished); QVERIFY(done.wait(5000));
        QCOMPARE(reply->error(),QNetworkReply::NoError); QCOMPARE(reply->readAll(),QByteArray("OK"));
        QVERIFY(request.startsWith("GET http://never-resolve.invalid/probe HTTP/1.1")); reply->deleteLater();
    }
};
QTEST_GUILESS_MAIN(ProxyTests)
#include "proxy_tests.moc"
