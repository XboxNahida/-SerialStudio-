#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QProcess>
#include <QSslSocket>
#include <QSslKey>
#include <QTemporaryDir>
#include <QStandardPaths>
#include "webdebugger.h"

class HttpsFixture : public QTcpServer {
public:
    QSslCertificate certificate;
    QSslKey key;
protected:
    void incomingConnection(qintptr descriptor) override {
        auto *socket=new QSslSocket(this); socket->setSocketDescriptor(descriptor);
        socket->setLocalCertificate(certificate); socket->setPrivateKey(key); socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        connect(socket,&QSslSocket::readyRead,this,[socket] { socket->readAll(); socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok"); socket->disconnectFromHost(); });
        socket->startServerEncryption();
    }
};

class WebTests : public QObject {
    Q_OBJECT
private slots:
    void requestAndResponse() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray received;
        connect(&server,&QTcpServer::newConnection,this,[&] {
            auto *socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,this,[&,socket] {
                received+=socket->readAll();
                if(!received.contains("\r\n\r\n") || !received.endsWith("测试请求")) return;
                const QByteArray body="测试响应";
                socket->write("HTTP/1.1 201 Created\r\nContent-Type: application/json; charset=utf-8\r\nX-Test: yes\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                socket->disconnectFromHost();
            });
        });
        WebDebugger web;
        QVERIFY(web.request({{"method","POST"},{"url",QString("http://127.0.0.1:%1/test?q=ok").arg(server.serverPort())},
            {"headers",QVariantList{QVariantMap{{"name","X-Input"},{"value","hello"}}}},{"body",QStringLiteral("测试请求")},{"contentType","application/json"}}));
        QVERIFY(web.busy()); QTRY_VERIFY_WITH_TIMEOUT(!web.busy(),3000);
        QCOMPARE(web.statusCode(),201); QCOMPARE(web.responseBody(),QStringLiteral("测试响应"));
        QVERIFY(web.error().isEmpty()); QVERIFY(web.elapsed()>=0);
        QVERIFY(received.startsWith("POST /test?q=ok HTTP/1.1\r\n"));
        QVERIFY(received.contains("X-Input: hello")); QVERIFY(!web.responseHeaders().isEmpty());
    }
    void rejectInvalidInput() {
        WebDebugger web;
        for(const auto &url:{"file:///C:/Windows/win.ini","ftp://127.0.0.1","invalid"}) {
            QVERIFY(!web.request({{"url",url}})); QVERIFY(!web.error().isEmpty());
        }
        QVERIFY(!web.request({{"url","http://127.0.0.1"},{"method","GET\r\nBad"}}));
        QVERIFY(!web.request({{"url","http://127.0.0.1"},{"headers",QVariantList{QVariantMap{{"name","Bad"},{"value","x\r\nInjected: yes"}}}}}));
    }
    void timeoutAndCancel() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        WebDebugger web;
        const QVariantMap config{{"url",QString("http://127.0.0.1:%1/").arg(server.serverPort())},{"timeout",40}};
        QVERIFY(web.request(config)); QTRY_VERIFY_WITH_TIMEOUT(!web.busy(),1500);
        QVERIFY(web.error().contains(QStringLiteral("超时")));
        QVERIFY(web.request(config)); web.cancel(); QTRY_VERIFY(!web.busy());
        QVERIFY(web.status().contains(QStringLiteral("取消")));
    }
    void headLargeResourceHasNoBody() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray received;
        connect(&server,&QTcpServer::newConnection,this,[&] {
            auto *socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,this,[&,socket] {
                received+=socket->readAll();
                if(!received.contains("\r\n\r\n")) return;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 16777216\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
            });
        });
        WebDebugger web;
        QVERIFY(web.request({{"method","HEAD"},{"url",QString("http://127.0.0.1:%1/large.bin").arg(server.serverPort())}}));
        QTRY_VERIFY_WITH_TIMEOUT(!web.busy(),3000);
        QVERIFY(received.startsWith("HEAD /large.bin HTTP/1.1\r\n"));
        QCOMPARE(web.statusCode(),200); QVERIFY(web.responseBody().isEmpty());
        QVERIFY2(web.error().isEmpty(),qPrintable(web.error()));
    }
    void responseLimit() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server,&QTcpServer::newConnection,this,[&] {
            auto *socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,this,[socket] {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 8388609\r\nConnection: close\r\n\r\n");
                socket->write(QByteArray(8388609,'x')); socket->disconnectFromHost();
            });
        });
        WebDebugger web; QVERIFY(web.request({{"url",QString("http://127.0.0.1:%1/").arg(server.serverPort())}}));
        QTRY_VERIFY_WITH_TIMEOUT(!web.busy(),5000);
        QVERIFY(web.error().contains("8 MiB")); QVERIFY(web.responseBody().size()<=8388608);
    }
    void streamedResponseAndHttpErrorBody() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        const QByteArray body(262144,'z');
        connect(&server,&QTcpServer::newConnection,this,[&] {
            auto *socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,this,[socket,body] {
                socket->readAll();
                socket->write("HTTP/1.1 404 Not Found\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
                for(int i=0;i<4;++i)socket->write("10000\r\n"+body.mid(i*65536,65536)+"\r\n");
                socket->write("0\r\n\r\n"); socket->disconnectFromHost();
            });
        });
        WebDebugger web; QVERIFY(web.request({{"url",QString("http://127.0.0.1:%1/").arg(server.serverPort())}}));
        QTRY_VERIFY_WITH_TIMEOUT(!web.busy(),3000);
        QCOMPARE(web.statusCode(),404); QCOMPARE(web.responseBody().toUtf8(),body);
    }
    void tlsFailureIsVisible() {
        auto executable=QStandardPaths::findExecutable("openssl");
        if(executable.isEmpty() && QFile::exists("C:/msys64/ucrt64/bin/openssl.exe"))executable="C:/msys64/ucrt64/bin/openssl.exe";
        if(executable.isEmpty() || !QSslSocket::supportsSsl())QSKIP("本机缺少 OpenSSL 证书生成器或 TLS 后端");
        QTemporaryDir directory; QProcess process;
        process.start(executable,{"req","-x509","-newkey","rsa:2048","-keyout",directory.filePath("key.pem"),"-out",directory.filePath("cert.pem"),"-days","2","-nodes","-subj","/CN=localhost","-addext","subjectAltName=DNS:localhost"});
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitCode(),0);
        QFile cert(directory.filePath("cert.pem")),key(directory.filePath("key.pem"));
        QVERIFY(cert.open(QIODevice::ReadOnly)); QVERIFY(key.open(QIODevice::ReadOnly));
        HttpsFixture server; server.certificate=QSslCertificate(cert.readAll()); server.key=QSslKey(key.readAll(),QSsl::Rsa);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        WebDebugger web; QVERIFY(web.request({{"url",QString("https://localhost:%1/").arg(server.serverPort())},{"timeout",4000}}));
        QTRY_VERIFY_WITH_TIMEOUT(!web.busy(),5000);
        QVERIFY(web.error().contains("TLS")); QCOMPARE(web.statusCode(),0); QVERIFY(web.responseBody().isEmpty());
    }
};
QTEST_GUILESS_MAIN(WebTests)
#include "web_tests.moc"
