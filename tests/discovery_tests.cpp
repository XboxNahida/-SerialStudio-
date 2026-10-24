#include <QtTest>
#include <QTcpServer>
#include <QUdpSocket>
#include "devicediscovery.h"

class DiscoveryTests : public QObject {
    Q_OBJECT
private slots:
    void stableIdentityAndDeduplication() {
        const QVariantMap saved{{"type","network"},{"address","127.0.0.1:8765"},{"name","保存目标"},{"state","known"}};
        auto available=saved; available["state"]="available";
        const auto result=DeviceDiscovery::normalizedDevices({saved,available});
        QCOMPARE(result.size(),1);
        QCOMPARE(result[0].toMap().value("state").toString(),QString("available"));
        QCOMPARE(result[0].toMap().value("id"),DeviceDiscovery::normalizedDevices({saved})[0].toMap().value("id"));
        const auto item=result[0].toMap();
        for(const auto &key:{"id","type","name","address","state","detail","actions","config"}) QVERIFY(item.contains(key));
        auto different=saved; different["type"]="adb";
        QCOMPARE(DeviceDiscovery::normalizedDevices({saved,different}).size(),2);
        auto renamed=saved; renamed["name"]=QStringLiteral("新名称");
        QCOMPARE(DeviceDiscovery::normalizedDevices({renamed})[0].toMap().value("id"),item.value("id"));
    }
    void subnetBounds() {
        QString error;
        QCOMPARE(DeviceDiscovery::subnetTargets("127.0.0.1/32",1,&error),QStringList{"127.0.0.1"});
        QCOMPARE(DeviceDiscovery::subnetTargets("192.168.1.0/24",4,&error).size(),254);
        for(const auto &cidr:{"0.0.0.0/0","10.0.0.0/23","8.8.8.8/32","100.64.0.0/24","::1/128","192.168.1.0/33","bad"}) {
            QVERIFY(DeviceDiscovery::subnetTargets(cidr,1,&error).isEmpty()); QVERIFY(!error.isEmpty());
        }
        QCOMPARE(DeviceDiscovery::subnetTargets("192.168.1.0/24",5,&error).size(),254);
        QCOMPARE(DeviceDiscovery::subnetTargets("192.168.1.0/24",8,&error).size(),254);
        QVERIFY(DeviceDiscovery::subnetTargets("192.168.1.0/24",9,&error).isEmpty());
        QCOMPARE(DeviceDiscovery::subnetTargets("127.0.0.1/32",2048,&error).size(),1);
        QVERIFY(DeviceDiscovery::subnetTargets("127.0.0.1/32",2049,&error).isEmpty());
        QVERIFY(DeviceDiscovery::subnetTargets("127.0.0.1/32",0,&error).isEmpty());
    }
    void historyIsNotOnline() {
        DeviceDiscovery discovery(nullptr,false);
        discovery.setSavedDevices({QVariantMap{{"type","network"},{"address","10.0.0.99:22"},{"state","connected"}}});
        QCOMPARE(discovery.devices().size(),1);
        QCOMPARE(discovery.devices()[0].toMap().value("state").toString(),QString("known"));
        discovery.setSavedDevices({}); QVERIFY(discovery.devices().isEmpty());
    }
    void announcementValidation() {
        const auto item=DeviceDiscovery::parseAnnouncement(R"({"service":"serialstudio-board","version":1,"name":"测试板","port":8765})",QHostAddress("192.168.2.3"));
        QCOMPARE(item.value("address").toString(),QString("192.168.2.3:8765"));
        QVERIFY(DeviceDiscovery::parseAnnouncement(R"({"service":"other","version":1,"port":22})",QHostAddress::LocalHost).isEmpty());
        QVERIFY(DeviceDiscovery::parseAnnouncement(R"({"service":"serialstudio-board","version":1,"port":65536})",QHostAddress::LocalHost).isEmpty());
        QVERIFY(DeviceDiscovery::parseAnnouncement("invalid",QHostAddress::LocalHost).isEmpty());
    }
    void loopbackProbeAndCancel() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        DeviceDiscovery discovery(nullptr,false);
        QVERIFY(discovery.scanSubnet("127.0.0.1/32",{server.serverPort()}));
        QTRY_VERIFY_WITH_TIMEOUT(!discovery.scanning(),2500);
        QCOMPARE(discovery.devices().size(),1);
        const auto item=discovery.devices()[0].toMap();
        QCOMPARE(item.value("state").toString(),QString("available"));
        QCOMPARE(item.value("config").toMap().value("port").toUInt(),uint(server.serverPort()));
        QVERIFY(discovery.scanSubnet("127.0.0.0/24",{server.serverPort()}));
        QCoreApplication::processEvents();
        discovery.cancelScan(); QVERIFY(!discovery.scanning());
        QVERIFY(discovery.status().contains(QStringLiteral("取消")));
        const auto count=discovery.devices().size(); QTest::qWait(550);
        QCOMPARE(discovery.devices().size(),count);
        QVERIFY(!discovery.scanSubnet("8.8.8.8/32",{22}));
        QVERIFY(!discovery.scanSubnet("127.0.0.1/32",{-1}));
    }
    void asynchronousLocalInventory() {
        DeviceDiscovery discovery(nullptr,false);
        QElapsedTimer elapsed; elapsed.start(); discovery.refresh();
        QVERIFY(elapsed.elapsed()<150);
        QVERIFY(discovery.scanning());
        QTRY_VERIFY_WITH_TIMEOUT(!discovery.scanning(),10000);
        QVERIFY(!discovery.interfaces().isEmpty());
        for(const auto &value:discovery.interfaces()) {
            const auto item=value.toMap();
            for(const auto &key:{"name","id","address","prefixLength","type","up"}) QVERIFY(item.contains(key));
            QVERIFY(QHostAddress(item.value("address").toString()).protocol()==QAbstractSocket::IPv4Protocol);
        }
        for(const auto &value:discovery.devices()) {
            const auto item=value.toMap();
            QVERIFY(item.value("state").toString()!="connected");
            if(item.value("type")=="volume") QVERIFY(item.value("config").toMap().contains("readOnly"));
            if(item.value("type")=="serial") QVERIFY(item.value("config").toMap().contains("port"));
            if(item.value("type")=="usb") QVERIFY(item.value("actions").toStringList().isEmpty());
        }
    }
    void broadcastReceive() {
        DeviceDiscovery discovery(nullptr,false,0);
        QVERIFY(discovery.listenPort()>0);
        QUdpSocket sender;
        const QByteArray packet=R"({"service":"serialstudio-board","version":1,"name":"回环测试广播","port":12345})";
        QCOMPARE(sender.writeDatagram(packet,QHostAddress::LocalHost,discovery.listenPort()),qint64(packet.size()));
        const auto seen=[&discovery] {
            for(const auto &entry:discovery.devices()) if(entry.toMap().value("address")=="127.0.0.1:12345") return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(seen(),2500);
    }
};
QTEST_GUILESS_MAIN(DiscoveryTests)
#include "discovery_tests.moc"
