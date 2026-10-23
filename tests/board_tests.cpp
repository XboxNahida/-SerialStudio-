#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QJsonDocument>
#include "boardcontroller.h"
#include "bridgecodec.h"

class BoardTests : public QObject {
    Q_OBJECT
private slots:
    void routedBridge_data()
    { QTest::addColumn<int>("mode"); QTest::newRow("JSON行") << 1; QTest::newRow("自定义帧") << 3; }
    void routedBridge()
    {
        QFETCH(int, mode);
        QTcpServer peer; QVERIFY(peer.listen(QHostAddress::LocalHost));
        Session serial(nullptr, false), network(nullptr, false);
        BoardController board(&serial, &network);
        QVERIFY(network.configureParser(mode, "AA55", "0D0A"));
        QVERIFY(network.openNetwork({{"mode",0},{"host","127.0.0.1"},{"port",peer.serverPort()}}));
        QTRY_VERIFY(network.writable()); QTRY_VERIFY(peer.hasPendingConnections());
        auto socket = peer.nextPendingConnection();
        const QVariantMap request{{"bus","i2c"},{"op","transfer"},{"device",1},{"address",80},{"tx","00"},{"read",2}};
        QVERIFY(board.execute(1, {}, request)); QVERIFY(board.busy());
        QTRY_VERIFY(socket->bytesAvailable()); QByteArray packet = socket->readAll();
        QString error;
        const auto object = BridgeCodec::decode(packet, mode, QByteArray::fromHex("aa55"), QByteArray::fromHex("0d0a"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error)); QCOMPARE(object.value("bus").toString(), QString("i2c"));
        QVERIFY(serial.terminal()->entries().isEmpty());
        auto response = object; response["id"] = 123456; response["ok"] = true; response["rx"] = "0102";
        auto encode = [&](const QJsonObject &o) { return BridgeCodec::encode(o, mode, QByteArray::fromHex("aa55"), QByteArray::fromHex("0d0a"), &error); };
        socket->write(encode(response)); QTest::qWait(30); QVERIFY(board.busy());
        response["id"] = object.value("id");
        packet = encode(response); socket->write(packet.left(2)); QTest::qWait(10); QVERIFY(board.busy());
        socket->write(packet.mid(2)); QTRY_VERIFY(!board.busy());
        QVERIFY(board.success()); QVERIFY(board.output().contains("0102"));
        QVERIFY(board.execute(1, {}, request)); board.cancel(); QVERIFY(!board.busy()); QVERIFY(!board.success());
        network.closePort(); QTRY_VERIFY(!network.connected());
    }
    void guards()
    {
        Session serial(nullptr, false), network(nullptr, false); BoardController board(&serial, &network);
        QVERIFY(!board.execute(0, {}, {{"bus","system"},{"op","info"}})); QVERIFY(!board.success());
        QVERIFY(!board.execute(2, {{"host","x;bad"},{"user","root"}}, {{"bus","system"},{"op","info"}}));
        QVERIFY(!board.busy());
        QVERIFY(!board.execute(1, {}, {{"bus","i2c"},{"op","transfer"},{"device",0},{"address",128},{"read",1}}));
        QVERIFY(!board.openTerminal({{"host","-oBad"},{"user","root"}}));
    }
    void udpReplyMustComeFromTarget()
    {
        QUdpSocket peer, other;
        QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
        QVERIFY(other.bind(QHostAddress::LocalHost, 0));
        Session serial(nullptr, false), network(nullptr, false);
        BoardController board(&serial, &network);
        QVERIFY(network.openNetwork({{"mode",2},{"host","127.0.0.1"},{"port",peer.localPort()},
                                     {"bind","127.0.0.1"},{"localPort",0}}));
        QTRY_VERIFY(network.writable());
        QVERIFY(board.execute(1, {}, {{"bus","system"},{"op","info"}}));
        QTRY_VERIFY(peer.hasPendingDatagrams());
        QByteArray packet(peer.pendingDatagramSize(), '\0');
        QHostAddress host; quint16 port = 0;
        QVERIFY(peer.readDatagram(packet.data(), packet.size(), &host, &port) > 0);
        auto object = QJsonDocument::fromJson(packet).object();
        object["ok"] = true;
        const auto reply = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        other.writeDatagram(reply, host, port);
        QTRY_VERIFY(network.rxBytes() > 0);
        QVERIFY(board.busy());
        peer.writeDatagram(reply, host, port);
        QTRY_VERIFY(!board.busy());
        QVERIFY(board.success());
        network.closePort(); QTRY_VERIFY(!network.connected());
    }
};
QTEST_GUILESS_MAIN(BoardTests)
#include "board_tests.moc"
