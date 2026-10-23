#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include "filetransfer.h"

class TransferTests: public QObject {
    Q_OBJECT
private slots:
    void copyAndRefuseOverwrite()
    {
        QTemporaryDir dir;QVERIFY(dir.isValid());QDir().mkpath(dir.filePath("usb"));
        QFile source(dir.filePath("test.bin"));QVERIFY(source.open(QIODevice::WriteOnly));
        const QByteArray data(700000,'x');QCOMPARE(source.write(data),qint64(data.size()));source.close();
        FileTransfer transfer;
        QVERIFY(transfer.start(0,QUrl::fromLocalFile(source.fileName()),QUrl::fromLocalFile(dir.filePath("usb")),{}));
        QTRY_VERIFY_WITH_TIMEOUT(!transfer.busy(),5000);QVERIFY2(transfer.success(),qPrintable(transfer.status()));
        QFile copied(dir.filePath("usb/test.bin"));QVERIFY(copied.open(QIODevice::ReadOnly));QCOMPARE(copied.readAll(),data);copied.close();
        QCOMPARE(transfer.progress(),100);
        QVERIFY(transfer.start(0,QUrl::fromLocalFile(source.fileName()),QUrl::fromLocalFile(dir.filePath("usb")),{}));
        QTRY_VERIFY(!transfer.busy());QVERIFY(!transfer.success());
        QVERIFY(copied.open(QIODevice::ReadOnly));QCOMPARE(copied.readAll(),data);
    }
    void invalidNetworkAndCancel()
    {
        FileTransfer transfer;
        QVERIFY(!transfer.start(2,{},QUrl::fromLocalFile("missing/file"),{{"host","bad;host"}}));
        QVERIFY(!transfer.busy());
        QStringList arguments;QByteArray batch;QString error;
        const QVariantMap config{{"host","192.0.2.1"},{"user","root"},{"port",22},{"remote","/tmp/a b.bin"},{"overwrite",true}};
        QVERIFY(FileTransfer::sftpCommands(1,"C:/a b.bin","/tmp/a b.bin.part",config,&arguments,&batch,&error));
        QVERIFY(batch.contains("put \"C:/a b.bin\" \"/tmp/a b.bin.part\""));
        QVERIFY(batch.contains("rename \"/tmp/a b.bin.part\" \"/tmp/a b.bin\""));
        QVERIFY(FileTransfer::sftpCommands(1,"C:/read[1].bin","/tmp/x",config,&arguments,&batch,&error));
        QVERIFY(batch.contains("\"C:/read[1].bin\""));
        auto special=config;special["remote"]="/tmp/a[1]*?.bin";
        QVERIFY(FileTransfer::sftpCommands(2,"C:/target","C:/temp",special,&arguments,&batch,&error));
        QVERIFY(batch.contains("\"/tmp/a[1]*?.bin\""));
        auto bad=config;bad["remote"]="/tmp/a\nrm x";
        QVERIFY(!FileTransfer::sftpCommands(1,"C:/a", "/tmp/x",bad,&arguments,&batch,&error));
        bad=config;bad["overwrite"]=false;
        QVERIFY(!FileTransfer::sftpCommands(1,"C:/a","/tmp/x",bad,&arguments,&batch,&error));
    }
};
QTEST_GUILESS_MAIN(TransferTests)
#include "transfer_tests.moc"
