#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include "networkworker.h"

class NetworkTests : public QObject {
    Q_OBJECT
private slots:
    void tcpClientRoundTrip()
    {
        QTcpServer peer; QVERIFY(peer.listen(QHostAddress::LocalHost));
        NetworkWorker worker;
        QSignalSpy opened(&worker, &NetworkWorker::opened), rx(&worker, &NetworkWorker::received);
        QSignalSpy tx(&worker, &NetworkWorker::transmitted), closed(&worker, &NetworkWorker::closed);
        worker.open({{"mode",0},{"host","127.0.0.1"},{"port",peer.serverPort()}});
        QTRY_COMPARE(opened.size(), 1);
        QTRY_VERIFY(peer.hasPendingConnections());
        auto socket = peer.nextPendingConnection();
        const auto message = QStringLiteral("网络中文\n").toUtf8();
        worker.write(message); QTRY_VERIFY(socket->bytesAvailable() > 0);
        QCOMPARE(socket->readAll(), message); QTRY_VERIFY(!tx.isEmpty());
        socket->write(message); QTRY_COMPARE(rx.size(), 1);
        QCOMPARE(rx[0][0].toByteArray(), message); worker.acknowledgeReceive();
        worker.close(); QCOMPARE(closed.size(), 1);
    }
    void tcpServerReconnect()
    {
        NetworkWorker worker;
        QSignalSpy ready(&worker, &NetworkWorker::opened), available(&worker, &NetworkWorker::availability);
        QSignalSpy rx(&worker, &NetworkWorker::received);
        worker.open({{"mode",1},{"host","127.0.0.1"},{"port",0}});
        QCOMPARE(ready.size(), 1); QVERIFY(!ready[0][1].toBool()); QVERIFY(worker.localPort());
        QTcpSocket client; client.connectToHost(QHostAddress::LocalHost, worker.localPort());
        QTRY_VERIFY(!available.isEmpty()); QVERIFY(available.last()[0].toBool());
        client.write("test\n"); QTRY_COMPARE(rx.size(), 1); worker.acknowledgeReceive();
        client.disconnectFromHost(); QTRY_VERIFY(!available.last()[0].toBool());
        QTcpSocket next; next.connectToHost(QHostAddress::LocalHost, worker.localPort());
        QTRY_VERIFY(available.last()[0].toBool());
        worker.write("reply"); QTRY_VERIFY(next.bytesAvailable()); QCOMPARE(next.readAll(), QByteArray("reply"));
        worker.close();
    }
    void udpRoundTrip()
    {
        QUdpSocket peer; QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
        NetworkWorker worker;
        QSignalSpy ready(&worker, &NetworkWorker::opened), rx(&worker, &NetworkWorker::received);
        worker.open({{"mode",2},{"host","127.0.0.1"},{"port",peer.localPort()},{"bind","127.0.0.1"},{"localPort",0}});
        QCOMPARE(ready.size(), 1); worker.write("udp");
        QTRY_VERIFY(peer.hasPendingDatagrams()); QByteArray b(3, '\0');
        QCOMPARE(peer.readDatagram(b.data(), b.size()), qint64(3)); QCOMPARE(b, QByteArray("udp"));
        peer.writeDatagram("back", QHostAddress::LocalHost, worker.localPort());
        QTRY_COMPARE(rx.size(), 1); QCOMPARE(rx[0][0].toByteArray(), QByteArray("back"));
        QVERIFY(rx[0][1].toBool());
        worker.acknowledgeReceive();
        QUdpSocket other; QVERIFY(other.bind(QHostAddress::LocalHost, 0));
        other.writeDatagram("other", QHostAddress::LocalHost, worker.localPort());
        QTRY_COMPARE(rx.size(), 2); QCOMPARE(rx[1][0].toByteArray(), QByteArray("other"));
        QVERIFY(!rx[1][1].toBool());
        worker.acknowledgeReceive(); worker.close();
    }
    void invalidConfig()
    {
        NetworkWorker worker; QSignalSpy errors(&worker, &NetworkWorker::fault);
        worker.open({{"mode",9},{"host","x"},{"port",-1}}); QCOMPARE(errors.size(), 1);
    }
};
QTEST_GUILESS_MAIN(NetworkTests)
#include "network_tests.moc"
