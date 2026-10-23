#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "payloaddecoder.h"
#include "dataworkspace.h"

class DataTests : public QObject {
    Q_OBJECT
private slots:
    void binaryFields()
    {
        PayloadDecoder decoder; QString error;
        QVERIFY(decoder.configure("binary", R"([{"name":"温度","offset":0,"type":"i16","endian":"le","scale":0.1},{"name":"电压","offset":2,"type":"u16","endian":"be","scale":0.001},{"name":"增益","offset":4,"type":"f32","endian":"le"}])", &error));
        const auto r = decoder.decode(QByteArray::fromHex("9cff0ce40000c03f"));
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.values.value("温度").toDouble(), -10.0);
        QCOMPARE(r.values.value("电压").toDouble(), 3.3);
        QCOMPARE(r.values.value("增益").toDouble(), 1.5);
        QVERIFY(!decoder.decode(QByteArray::fromHex("01")).error.isEmpty());
        QVERIFY(!decoder.configure("binary", R"([{"name":"x","type":"u16","offset":-1}])", &error));
        QVERIFY(!decoder.configure("binary", R"([{"name":"x","type":"u8","offset":0},{"name":"x","type":"u8","offset":1}])", &error));
    }
    void textFormats()
    {
        PayloadDecoder d; QString error;
        QVERIFY(d.configure("json", "[]", &error));
        const auto json = d.decode(R"({"temperature":26.5,"status":"OK"})");
        QCOMPARE(json.values.value("temperature").toDouble(), 26.5);
        QVERIFY(!d.decode("bad").error.isEmpty());
        QVERIFY(d.configure("csv", "[]", &error));
        const auto csv = d.decode("1,\"two,three\",3.5\r\n");
        QVERIFY(csv.error.isEmpty()); QCOMPARE(csv.values.value("列2").toString(), QString("two,three"));
        QCOMPARE(csv.values.value("列3").toDouble(), 3.5);
        QVERIFY(!d.decode("\"unclosed").error.isEmpty());
    }
    void captureAndBounds()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); DataWorkspace workspace;
        QVERIFY(workspace.configure("json", "[]"));
        for (int i=0; i<300; ++i) workspace.ingest(QByteArray("{\"x\":")+QByteArray::number(i)+'}');
        QCOMPARE(workspace.samples().size(), 256);
        QCOMPARE(workspace.fields(), QStringList{"x"});
        const auto path = QUrl::fromLocalFile(dir.filePath("data.bin"));
        QVERIFY(workspace.beginCapture(path));
        workspace.ingest("DEMO",false);
        workspace.ingest("abc"); workspace.ingest("def");
        QVERIFY(workspace.finishCapture());
        QFile file(path.toLocalFile()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("abcdef"));
        QVERIFY(!workspace.beginCapture(path));
        QVERIFY(workspace.beginCapture(QUrl::fromLocalFile(dir.filePath("cancel.bin"))));
        workspace.ingest("discard"); workspace.cancelCapture();
        QVERIFY(!QFile::exists(dir.filePath("cancel.bin")));
        QVERIFY(!workspace.openFile(QUrl("https://example.com/x"), "auto"));
    }
    void largeJsonFile()
    {
        QTemporaryDir dir;QFile file(dir.filePath("large.json"));QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray("{\"text\":\"")+QByteArray(70000,'x')+"\",\"value\":2}");file.close();
        DataWorkspace workspace;QVERIFY(workspace.openFile(QUrl::fromLocalFile(file.fileName()),"json"));
        QVERIFY(workspace.text().size()<33000);QCOMPARE(workspace.fields(),QStringList{"value"});
    }
};
QTEST_GUILESS_MAIN(DataTests)
#include "data_tests.moc"
