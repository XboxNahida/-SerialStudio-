#include <QtTest>
#include "protocol.h"
#include "bridgecodec.h"

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void bridgeUsesExistingFraming()
    {
        const QJsonObject request{{"id",1},{"bus","i2c"},{"op","transfer"},{"tx","00"},{"read",2}};
        for (const int mode : {1,3}) {
            const auto head = QByteArray::fromHex("aa55"), tail = QByteArray::fromHex("0d0a");
            QString error;
            const auto packet = BridgeCodec::encode(request, mode, head, tail, &error);
            QVERIFY2(!packet.isEmpty(), qPrintable(error));
            ProtocolParser parser; parser.configure(ProtocolParser::Mode(mode), head, tail);
            QVERIFY(parser.feed(packet.left(3)).isEmpty());
            const auto frames = parser.feed(packet.mid(3)); QCOMPARE(frames.size(), 1);
            QCOMPARE(BridgeCodec::decode(frames[0].bytes, mode, head, tail, &error), request);
        }
        QString error;
        QVERIFY(BridgeCodec::encode(request, 2, {}, {}, &error).isEmpty()); QVERIFY(!error.isEmpty());
        QVERIFY(BridgeCodec::encode(request, 3, "H", "i2c", &error).isEmpty());
        QVERIFY(BridgeCodec::decode("not json", 1, {}, {}, &error).isEmpty());
    }
    void strictHex()
    {
        QByteArray out; QString error;
        QVERIFY(decodeHex(QStringLiteral("01 03\n00\tff"), &out, &error));
        QCOMPARE(out.toHex(), QByteArray("010300ff"));
        for (const QString &bad : {QStringLiteral("ABC"), QStringLiteral("0x01"), QStringLiteral("GG"), QStringLiteral("01,02")}) {
            QVERIFY(!decodeHex(bad, &out, &error));
            QVERIFY(!error.isEmpty());
            QVERIFY(out.isEmpty());
        }
        QVERIFY(!decodeHex(QStringLiteral(""), &out, &error));
    }
    void crcKnownVector()
    {
        QCOMPARE(crc16(QByteArray::fromHex("01030000000a")), quint16(0xcdc5));
        QCOMPARE(crc16(QByteArray::fromHex("01030000000ac5cd")), quint16(0));
    }
    void splitAndJoinedLines()
    {
        ProtocolParser p; p.configure(ProtocolParser::Lines);
        QVERIFY(p.feed("hel").isEmpty());
        auto frames = p.feed("lo\r\n{\"t\":25}\n");
        QCOMPARE(frames.size(), 2);
        QCOMPARE(frames[0].bytes, QByteArray("hello\r\n"));
        QVERIFY(frames[1].title.contains("JSON"));
        QVERIFY(frames[1].detail.contains("25"));
    }
    void unicodeSplitAcrossReads()
    {
        ProtocolParser p; p.configure(ProtocolParser::Lines);
        auto bytes = QStringLiteral("温度正常\n").toUtf8();
        QVERIFY(p.feed(bytes.left(2)).isEmpty());
        auto f = p.feed(bytes.mid(2));
        QCOMPARE(f.size(), 1);
        QCOMPARE(f[0].detail, QStringLiteral("温度正常"));
    }
    void modbusSplitAndJoined()
    {
        ProtocolParser p; p.configure(ProtocolParser::Modbus);
        auto request = QByteArray::fromHex("01030000000ac5cd");
        QVERIFY(p.feed(request.left(3)).isEmpty());
        auto f = p.feed(request.mid(3) + request);
        QCOMPARE(f.size(), 2);
        QVERIFY(f[0].valid); QVERIFY(f[1].valid);
        QVERIFY(f[0].detail.contains("10"));
        QCOMPARE(p.bufferedSize(), qsizetype(0));
    }
    void modbusResponseAndException()
    {
        ProtocolParser p; p.configure(ProtocolParser::Modbus);
        auto f = p.feed(QByteArray::fromHex("010302000a3843"));
        QCOMPARE(f.size(), 1); QVERIFY(f[0].valid);
        QVERIFY(f[0].detail.contains("10"));
        auto exception = QByteArray::fromHex("018302");
        const auto crc = crc16(exception);
        exception.append(char(crc & 255)); exception.append(char(crc >> 8));
        f = p.feed(exception);
        QCOMPARE(f.size(), 1); QVERIFY(f[0].valid);
        QVERIFY(f[0].title.contains(QStringLiteral("异常")));
    }
    void badCrcAndResync()
    {
        ProtocolParser p; p.configure(ProtocolParser::Modbus);
        const auto good = QByteArray::fromHex("01030000000ac5cd");
        p.feed(QByteArray::fromHex("01030000000ac500"));
        auto f = p.flush();
        QCOMPARE(f.size(), 1); QVERIFY(!f[0].valid);
        f = p.feed(QByteArray::fromHex("ffff") + good);
        QVERIFY(f.size() >= 2); QVERIFY(!f[0].valid); QVERIFY(f.last().valid);
        QCOMPARE(f.last().bytes, good);
    }
    void customResync()
    {
        ProtocolParser p;
        p.configure(ProtocolParser::Custom, QByteArray::fromHex("aa55"), QByteArray::fromHex("0d0a"));
        p.feed(QByteArray::fromHex("00aa"));
        auto f = p.feed(QByteArray::fromHex("5511220d0aaa55330d0a"));
        QVERIFY(f.size() >= 2);
        QCOMPARE(f[f.size()-2].bytes.toHex(), QByteArray("aa5511220d0a"));
        QCOMPARE(f.last().bytes.toHex(), QByteArray("aa55330d0a"));
        QVERIFY(f.last().valid);
    }
    void resetAndBounds()
    {
        ProtocolParser p; p.configure(ProtocolParser::Lines);
        auto f = p.feed(QByteArray(70000, 'x'));
        QVERIFY(p.bufferedSize() <= 65536);
        QVERIFY(!f.isEmpty()); QVERIFY(!f[0].valid);
        p.reset(); QCOMPARE(p.bufferedSize(), qsizetype(0));
        p.feed("left"); p.configure(ProtocolParser::Lines);
        f = p.feed("right\n");
        QCOMPARE(f.size(), 1); QCOMPARE(f[0].detail, QStringLiteral("right"));
    }
    void allCommonModbusFunctions()
    {
        for (const auto &body : {"010100000008", "010201a5", "01050001ff00", "01060001002a",
                                 "010f0000000801a5", "0110000000020400010002", "011000000002", "010402000a"}) {
            ProtocolParser p; p.configure(ProtocolParser::Modbus);
            auto packet = QByteArray::fromHex(body);
            const auto crc = crc16(packet);
            packet.append(char(crc & 255)); packet.append(char(crc >> 8));
            const auto f = p.feed(packet);
            QCOMPARE(f.size(), 1); QVERIFY(f[0].valid); QCOMPARE(f[0].bytes, packet);
        }
    }
    void modbusRequestWithResponseLength()
    {
        for (const auto &body : {"01030300000a", "01040300000a", "01010300000a"}) {
            ProtocolParser p; p.configure(ProtocolParser::Modbus);
            auto packet = QByteArray::fromHex(body);
            const auto crc = crc16(packet);
            packet.append(char(crc & 255)); packet.append(char(crc >> 8));
            const auto frames = p.feed(packet);
            QCOMPARE(frames.size(), 1);
            QVERIFY(frames[0].detail.contains(QStringLiteral("768")));
            if (packet[1] == 1) QVERIFY(frames[0].detail.contains(QStringLiteral("歧义")));
            else QVERIFY(!frames[0].detail.contains(QStringLiteral("读取响应")));
        }
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
