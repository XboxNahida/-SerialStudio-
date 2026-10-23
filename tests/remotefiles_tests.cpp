#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QProcess>
#include "remotefiles.h"

class RemoteFilesTests: public QObject {
    Q_OBJECT
private slots:
    void deleteListedEntriesAcrossBackends() {
        for(const auto &kind:QStringList{"local","mtp","adb"}) {
            QTemporaryDir dir(QCoreApplication::applicationDirPath()+"/delete-endpoint-XXXXXX");QVERIFY(dir.isValid());
            QVERIFY(QDir().mkpath(dir.filePath("tree/sub")));QFile file(dir.filePath("tree/sub/file"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("data");file.close();
            auto path=dir.path();QVariantMap options;
            if(kind=="adb"){path="/"+path.left(1).toLower()+path.mid(2);options={{"adb",QCoreApplication::applicationFilePath()},{"serial","fixture"}};}
            RemoteFiles browser;QVERIFY(browser.browse(kind,path,options));QTRY_VERIFY(!browser.busy());QVERIFY(browser.success());
            QCOMPARE(browser.entries().size(),1);const auto id=browser.entries()[0].toMap().value("id").toString();
            QVERIFY(!browser.removeEntry(id));QVERIFY(QDir(dir.filePath("tree")).exists());
            QVERIFY(!browser.removeEntry("not-listed",true));QVERIFY(browser.removeEntry(id,true));QTRY_VERIFY(!browser.busy());
            QVERIFY2(browser.success(),qPrintable(kind+": "+browser.status()));QVERIFY(!QDir(dir.filePath("tree")).exists());QVERIFY(browser.entries().isEmpty());
        }
    }
    void localClipboardAndConfirmedDelete() {
        QTemporaryDir dir(QCoreApplication::applicationDirPath()+"/clipboard-local-XXXXXX");QVERIFY(dir.isValid());
        QVERIFY(QDir().mkpath(dir.filePath("out")));
        const auto name=QStringLiteral("中文 [1] '$.bin");QFile file(dir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));file.write("original");file.close();
        RemoteFiles browser;QVERIFY(!browser.paste());
        QVERIFY(browser.browse("local",dir.path()));QTRY_VERIFY(!browser.busy());QVERIFY(browser.success());
        QVERIFY(!browser.copyEntry("not-listed",true));QVERIFY(!browser.removeEntry(file.fileName(),false));QVERIFY(file.exists());
        QVERIFY(browser.copyEntry(file.fileName(),true));QCOMPARE(browser.clipboardName(),name);QVERIFY(browser.clipboardCut());QVERIFY(file.exists());
        QVERIFY(!browser.paste());QVERIFY(file.exists());
        QVERIFY(browser.browse("local",dir.filePath("out")));QTRY_VERIFY(!browser.busy());
        QFile collision(dir.filePath("out/"+name));QVERIFY(collision.open(QIODevice::WriteOnly));collision.write("keep");collision.close();
        QVERIFY(browser.paste());QTRY_VERIFY(!browser.busy());QVERIFY(!browser.success());QVERIFY(file.exists());QVERIFY(!browser.clipboardName().isEmpty());
        QVERIFY(collision.remove());QVERIFY(browser.paste());QTRY_VERIFY(!browser.busy());QVERIFY2(browser.success(),qPrintable(browser.status()));
        QVERIFY(!file.exists());QVERIFY(browser.clipboardName().isEmpty());QVERIFY(collision.open(QIODevice::ReadOnly));QCOMPARE(collision.readAll(),QByteArray("original"));collision.close();
        QCOMPARE(browser.entries().size(),1);
        QVERIFY(browser.removeEntry(collision.fileName(),true));QTRY_VERIFY(!browser.busy());QVERIFY(browser.success());QVERIFY(!collision.exists());QVERIFY(browser.entries().isEmpty());
        QVERIFY(QDir().mkpath(dir.filePath("out/tree/sub")));QFile child(dir.filePath("out/tree/sub/file"));QVERIFY(child.open(QIODevice::WriteOnly));child.write("data");child.close();
        QVERIFY(browser.browse("local",dir.filePath("out")));QTRY_VERIFY(!browser.busy());
        QVERIFY(browser.removeEntry(dir.filePath("out/tree"),true));QTRY_VERIFY(!browser.busy());QVERIFY(browser.success());QVERIFY(!QDir(dir.filePath("out/tree")).exists());
    }
    void clipboardAcrossAdbAndLocal() {
        QTemporaryDir dir(QCoreApplication::applicationDirPath()+"/clipboard-adb-XXXXXX");QVERIFY(dir.isValid());
        QVERIFY(QDir().mkpath(dir.filePath("remote")));QVERIFY(QDir().mkpath(dir.filePath("out")));
        const auto name=QStringLiteral("中文 [1] '$.bin");const QByteArray bytes("payload\0\xff",9);
        QFile source(dir.filePath(name));QVERIFY(source.open(QIODevice::WriteOnly));source.write(bytes);source.close();
        auto remote=dir.filePath("remote");remote="/"+remote.left(1).toLower()+remote.mid(2);
        const QVariantMap config{{"adb",QCoreApplication::applicationFilePath()},{"serial","fixture"}};
        RemoteFiles browser;QVERIFY(browser.browse("local",dir.path()));QTRY_VERIFY(!browser.busy());QVERIFY(browser.copyEntry(source.fileName()));
        QVERIFY(browser.browse("adb",remote,config));QTRY_VERIFY(!browser.busy());QVERIFY(browser.paste());QTRY_VERIFY_WITH_TIMEOUT(!browser.busy(),15000);QVERIFY2(browser.success(),qPrintable(browser.status()));
        QVERIFY(source.exists());QCOMPARE(browser.entries().size(),1);QVERIFY(browser.copyEntry(browser.entries()[0].toMap().value("id").toString(),true));
        QVERIFY(browser.browse("local",dir.filePath("out")));QTRY_VERIFY(!browser.busy());QVERIFY(browser.paste());QTRY_VERIFY_WITH_TIMEOUT(!browser.busy(),15000);QVERIFY2(browser.success(),qPrintable(browser.status()));
        QVERIFY(!QFileInfo::exists(dir.filePath("remote/"+name)));QFile copy(dir.filePath("out/"+name));QVERIFY(copy.open(QIODevice::ReadOnly));QCOMPARE(copy.readAll(),bytes);
    }
    void clipboardMtpShellAndMissingSource() {
        QTemporaryDir dir(QCoreApplication::applicationDirPath()+"/clipboard-shell-XXXXXX");QVERIFY(dir.isValid());
        QVERIFY(QDir().mkpath(dir.filePath("out")));QFile source(dir.filePath(QStringLiteral("便携文件.bin")));QVERIFY(source.open(QIODevice::WriteOnly));source.write("data");source.close();
        RemoteFiles browser;QVERIFY(browser.browse("mtp",dir.path()));QTRY_VERIFY(!browser.busy());QVERIFY(browser.success());
        QString sourceId;for(const auto &value:browser.entries())if(value.toMap().value("name").toString()==QFileInfo(source).fileName())sourceId=value.toMap().value("id").toString();
        QVERIFY2(browser.copyEntry(sourceId,true),qPrintable(browser.status()));QVERIFY(browser.browse("local",dir.filePath("out")));QTRY_VERIFY(!browser.busy());
        QVERIFY(browser.paste());QTRY_VERIFY(!browser.busy());QVERIFY2(browser.success(),qPrintable(browser.status()));QVERIFY(!source.exists());
        const auto copied=dir.filePath("out/"+QFileInfo(source).fileName());
        QVERIFY(browser.copyEntry(copied,true));QVERIFY(QFile::remove(copied));
        QVERIFY(browser.browse("local",dir.path()));QTRY_VERIFY(!browser.busy());QVERIFY(browser.paste());QTRY_VERIFY(!browser.busy());QVERIFY(!browser.success());
        QVERIFY(!QFileInfo::exists(source.fileName()));QVERIFY(!browser.clipboardName().isEmpty());
    }
    void failedVerificationAndCancellationKeepSource() {
        QTemporaryDir dir(QCoreApplication::applicationDirPath()+"/clipboard-verify-XXXXXX");QVERIFY(dir.isValid());
        QVERIFY(QDir().mkpath(dir.filePath("remote")));QFile source(dir.filePath("file.bin"));QVERIFY(source.open(QIODevice::WriteOnly));source.write("keep source");source.close();
        RemoteFiles browser;QVERIFY(browser.browse("local",dir.path()));QTRY_VERIFY(!browser.busy());QVERIFY(browser.copyEntry(source.fileName(),true));
        auto remote=dir.filePath("remote");remote="/"+remote.left(1).toLower()+remote.mid(2);
        const QVariantMap config{{"adb",QCoreApplication::applicationFilePath()},{"serial","fixture"}};
        QVERIFY(browser.browse("adb",remote,config));QTRY_VERIFY(!browser.busy());
        qputenv("SERIALSTUDIO_CORRUPT_PUSH","1");const bool started=browser.paste();QVERIFY(started);QTRY_VERIFY_WITH_TIMEOUT(!browser.busy(),15000);qunsetenv("SERIALSTUDIO_CORRUPT_PUSH");
        QVERIFY(!browser.success());QVERIFY(browser.status().contains("SHA256"));QVERIFY(source.exists());QVERIFY(!browser.clipboardName().isEmpty());
        QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(source.readAll(),QByteArray("keep source"));source.close();
        RemoteFileWorker worker;worker.cancelled.store(true);QSignalSpy done(&worker,&RemoteFileWorker::completed);
        worker.execute({{"action","delete"},{"kind","local"},{"path",dir.path()},{"entry",QVariantMap{{"id",source.fileName()},{"directory",false}}}});
        QCOMPARE(done.size(),1);QVERIFY(!done.first().first().toMap().value("error").toString().isEmpty());QVERIFY(source.exists());
    }
    void adbWorkerWithRealShellAndFileProcess() {
        const QString bash="C:/Program Files/Git/usr/bin/bash.exe";QVERIFY(QFileInfo::exists(bash));
        QTemporaryDir sandbox;QVERIFY(sandbox.isValid());
        QVERIFY(QDir().mkpath(sandbox.filePath("remote")));QVERIFY(QDir().mkpath(sandbox.filePath("download")));
        const QString name=QStringLiteral("中文 a'$ file.bin"),source=sandbox.filePath(name);
        QFile input(source);QVERIFY(input.open(QIODevice::WriteOnly));const QByteArray bytes("binary\0\xff",8);input.write(bytes);input.close();
        auto remote=sandbox.filePath("remote");remote="/"+remote.left(1).toLower()+remote.mid(2);
        RemoteFiles browser;const QVariantMap config{{"adb",QCoreApplication::applicationFilePath()},{"serial","fixture"}};
        QVERIFY(browser.browse("adb",remote,config));QTRY_VERIFY_WITH_TIMEOUT(!browser.busy(),10000);QVERIFY2(browser.success(),qPrintable(browser.status()));
        QCOMPARE(browser.entries().size(),0);
        QVERIFY(browser.upload(source));QTRY_VERIFY_WITH_TIMEOUT(!browser.busy(),10000);QVERIFY2(browser.success(),qPrintable(browser.status()));
        QVERIFY(browser.browse("adb",remote,config));QTRY_VERIFY_WITH_TIMEOUT(!browser.busy(),10000);QVERIFY(browser.success());
        QCOMPARE(browser.entries().size(),1);const auto row=browser.entries().first().toMap();QCOMPARE(row.value("name").toString(),name);
        QVERIFY(browser.download(row.value("id").toString(),sandbox.filePath("download")));QTRY_VERIFY_WITH_TIMEOUT(!browser.busy(),10000);QVERIFY2(browser.success(),qPrintable(browser.status()));
        QFile downloaded(sandbox.filePath("download/"+name));QVERIFY(downloaded.open(QIODevice::ReadOnly));QCOMPARE(downloaded.readAll(),bytes);
        QVERIFY(browser.upload(source));QTRY_VERIFY_WITH_TIMEOUT(!browser.busy(),10000);QVERIFY(!browser.success());
        QFile original(sandbox.filePath("remote/"+name));QVERIFY(original.open(QIODevice::ReadOnly));QCOMPARE(original.readAll(),bytes);
    }
    void localBrowseAndRoundTrip() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QDir root(dir.path());
        QVERIFY(root.mkdir(QStringLiteral("目录 空格")));QVERIFY(root.mkdir("out"));
        QFile file(dir.filePath(QStringLiteral("中文 ' $ test.bin")));QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray payload("abc\0data",8);QCOMPARE(file.write(payload),qint64(8));file.close();
        RemoteFiles browser;
        QVERIFY(browser.browse("local",dir.path()));QTRY_VERIFY(!browser.busy());
        QVERIFY2(browser.success(),qPrintable(browser.status()));
        QVariantMap found;for(const auto &v:browser.entries())if(v.toMap().value("name")==file.fileName().section('/',-1))found=v.toMap();
        QVERIFY(!found.isEmpty());QVERIFY(!found.value("directory").toBool());
        QVERIFY(browser.download(found.value("id").toString(),dir.filePath("out")));QTRY_VERIFY(!browser.busy());QVERIFY(browser.success());
        QFile copy(dir.filePath("out/"+found.value("name").toString()));QVERIFY(copy.open(QIODevice::ReadOnly));QCOMPARE(copy.readAll(),payload);copy.close();
        QVERIFY(browser.download(found.value("id").toString(),dir.filePath("out")));QTRY_VERIFY(!browser.busy());QVERIFY(!browser.success());
        QVERIFY(!browser.download("not-in-this-list",dir.filePath("out")));
        browser.reset();QVERIFY(browser.entries().isEmpty());QVERIFY(!browser.upload(file.fileName()));
    }
    void adbBinaryNamesAndPermissions() {
        QString error;QByteArray data("SSFM1\0",6);
        for(const auto &s:QStringList{"d",QStringLiteral("中文 空格"),"0","f",QStringLiteral("a'$.txt"),"32"}){data+=s.toUtf8();data+='\0';}
        const auto rows=RemoteFiles::parseAdbListing(data,"/sdcard",&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(rows.size(),2);
        QCOMPARE(rows[0].toMap().value("id").toString(),QStringLiteral("/sdcard/中文 空格"));
        QCOMPARE(rows[1].toMap().value("size").toLongLong(),32);
        RemoteFiles::parseAdbListing("permission denied","/",&error);QVERIFY(!error.isEmpty());
        QVERIFY(!RemoteFiles::validRemotePath("/sdcard/a\nb"));
        QCOMPARE(RemoteFiles::shellQuote("/a'b$"),QString("'/a'\"'\"'b$'"));
    }
    void sftpSpacesAndUnsafeLines() {
        QString error;
        const auto rows=RemoteFiles::parseSftpListing("sftp> ls -lna \"/home\"\r\ndrwxr-xr-x 2 1000 1000 4096 Oct 23 12:00 a directory\r\n-rw-r--r-- 1 1000 1000 42 Oct 23 2026 file with spaces.txt\r\n", "/home", &error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(rows.size(),2);
        QCOMPARE(rows[1].toMap().value("name").toString(),QString("file with spaces.txt"));
        RemoteFiles::parseSftpListing("unrecognised output", "/", &error);QVERIFY(!error.isEmpty());
        const auto leading=RemoteFiles::parseSftpListing("-rw-r--r-- 1 0 0 12 Oct 23 12:00  leading.txt\n","/",&error);
        QVERIFY(error.isEmpty());QCOMPARE(leading.first().toMap().value("name").toString(),QString(" leading.txt"));
        RemoteFiles::parseSftpListing("-rw-r--r-- 1 0 0 9999999999999999999999999 Oct 23 12:00 file\n","/",&error);QVERIFY(!error.isEmpty());
    }
    void invalidUtf8CannotAliasAnotherFile() {
        QString error;QByteArray adb("SSFM1\0",6);
        for(const auto &name:QList<QByteArray>{QByteArray::fromHex("ff"),QByteArray::fromHex("efbfbd")})
            for(const auto &field:QList<QByteArray>{"f",name,"1"}){adb+=field;adb+='\0';}
        QVERIFY(RemoteFiles::parseAdbListing(adb,"/data",&error).isEmpty());QVERIFY(error.contains("UTF-8"));
        QByteArray sftp="-rw-r--r-- 1 0 0 1 Oct 23 12:00 ";sftp+=QByteArray::fromHex("ff");
        sftp+="\n-rw-r--r-- 1 0 0 1 Oct 23 12:00 ";sftp+=QByteArray::fromHex("efbfbd");sftp+='\n';
        QVERIFY(RemoteFiles::parseSftpListing(sftp,"/data",&error).isEmpty());QVERIFY(error.contains("UTF-8"));
    }
    void ambiguousSftpLinkCannotDeleteSibling() {
        QString error;const auto rows=RemoteFiles::parseSftpListing(
            "-rw-r--r-- 1 0 0 1 Oct 23 12:00 a\nlrwxrwxrwx 1 0 0 1 Oct 23 12:00 a -> b -> target\n","/data",&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(rows.size(),2);
        QCOMPARE(rows[0].toMap().value("id").toString(),QString("/data/a"));
        const auto link=rows[1].toMap();QVERIFY(link.value("link").toBool());
        QVERIFY(link.value("id").toString().isEmpty());QVERIFY(!link.value("deletable",true).toBool());
        RemoteFileWorker worker;QSignalSpy done(&worker,&RemoteFileWorker::completed);
        worker.execute({{"action","delete"},{"kind","sftp"},{"path","/data"},{"entry",link}});
        QCOMPARE(done.size(),1);QVERIFY(!done.first().first().toMap().value("error").toString().isEmpty());
    }
    void sftpUnknownLinkCountAndSpecialFiles() {
        QString error;
        const auto rows=RemoteFiles::parseSftpListing("-rw-r--r-- ? 0 0 4 Oct 23 12:00 normal.bin\nprw------- 1 0 0 0 Oct 23 12:00 fifo\nsrw------- 1 0 0 0 Oct 23 12:00 socket\ncrw------- 1 0 0 1, 3 Oct 23 12:00 device\n", "/", &error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(rows.size(),1);
        QCOMPARE(rows.first().toMap().value("name").toString(),QString("normal.bin"));
    }
    void sftpActualLocalServerListing() {
        QTemporaryDir sandbox(QDir::tempPath()+"/SerialStudio-sftp [1]-XXXXXX");QVERIFY(sandbox.isValid());
        const auto name=QStringLiteral(" 中文 file [1].txt");
        QFile file(sandbox.filePath(name));QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("data"),qint64(4));file.close();
        auto path=sandbox.path();path="/"+path.left(1).toLower()+path.mid(2);
        QProcess process;process.start("C:/Program Files/Git/usr/bin/bash.exe",{"-c","export PATH=/usr/bin:/bin; exec sftp -D /usr/lib/ssh/sftp-server.exe -b -"});
        QVERIFY(process.waitForStarted());process.write(RemoteFiles::sftpListBatch(path));process.closeWriteChannel();
        QVERIFY(process.waitForFinished(15000));const auto output=process.readAllStandardOutput();
        QVERIFY2(process.exitCode()==0,(process.readAllStandardError()+output).constData());QString error;
        const auto rows=RemoteFiles::parseSftpListing(output,path,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(rows.size(),1);QCOMPARE(rows.first().toMap().value("name").toString(),name);
        QCOMPARE(rows.first().toMap().value("size").toLongLong(),4);
    }
    void sftpNativeWindowsClientRoundTrip() {
        QTemporaryDir sandbox(QCoreApplication::applicationDirPath()+QStringLiteral("/SFTP 中文 [1]-XXXXXX"));QVERIFY(sandbox.isValid());
        const auto name=QStringLiteral(" 中文 file [1].bin");
        const QByteArray bytes("data\0\xff",6);
        QFile file(sandbox.filePath(name));QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),bytes.size());file.close();
        auto path=sandbox.path();path="/"+path.left(1).toLower()+path.mid(2);
        const auto client=QDir(qEnvironmentVariable("SystemRoot","C:/Windows")).filePath("System32/OpenSSH/sftp.exe");
        QVERIFY(QFileInfo::exists(client));
        auto execute=[&](const QByteArray &batch,QByteArray *output) {
            QProcess process;auto env=QProcessEnvironment::systemEnvironment();
            env.insert("PATH","C:/Program Files/Git/usr/bin;"+env.value("PATH"));process.setProcessEnvironment(env);
            process.start(client,{"-D","\"C:/Program Files/Git/usr/lib/ssh/sftp-server.exe\"","-b","-"});
            if(!process.waitForStarted())return false;
            process.write(batch);process.closeWriteChannel();
            if(!process.waitForFinished(15000))return false;
            *output=process.readAllStandardOutput();
            if(process.exitCode()!=0)*output+=process.readAllStandardError();
            return process.exitCode()==0 && process.exitStatus()==QProcess::NormalExit;
        };
        QByteArray output;QVERIFY(execute(RemoteFiles::sftpListBatch(path),&output));QString error;
        const auto rows=RemoteFiles::parseSftpListing(output,path,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(rows.size(),1);QCOMPARE(rows.first().toMap().value("name").toString(),name);
        QCOMPARE(rows.first().toMap().value("size").toLongLong(),bytes.size());
        QVERIFY(QDir().mkpath(sandbox.filePath(QStringLiteral("下载 [2]"))));
        QStringList args;QByteArray batch;const auto destination=sandbox.filePath(QStringLiteral("下载 [2]/副本.bin"));
        QVERIFY(FileTransfer::sftpCommands(2,destination,destination,{{"host","fixture"},{"user","user"},{"remote",path+"/"+name}},&args,&batch,&error));
        QVERIFY2(execute(batch,&output),(batch+output).constData());QFile copy(destination);QVERIFY(copy.open(QIODevice::ReadOnly));QCOMPARE(copy.readAll(),bytes);
        copy.close();QVERIFY(QDir().mkpath(sandbox.filePath("upload")));
        const auto uploaded=path+"/upload/"+name;
        QVERIFY(FileTransfer::sftpCommands(1,file.fileName(),uploaded+".part",{{"host","fixture"},{"user","user"},{"remote",uploaded},{"overwrite",true}},&args,&batch,&error));
        QVERIFY2(execute(batch,&output),(batch+output).constData());
        QFile uploadedFile(sandbox.filePath("upload/"+name));QVERIFY(uploadedFile.open(QIODevice::ReadOnly));QCOMPARE(uploadedFile.readAll(),bytes);
        uploadedFile.close();
        // 模拟目录预检结束后出现同名目标，提交必须原子拒绝覆盖。
        QFile collision(sandbox.filePath("upload/"+name));QVERIFY(collision.open(QIODevice::WriteOnly));collision.write("keep collision");collision.close();
        QVERIFY(FileTransfer::sftpCommands(1,file.fileName(),uploaded+".safe.part",{{"host","fixture"},{"user","user"},{"remote",uploaded},{"overwrite",true},{"noClobber",true}},&args,&batch,&error));
        QVERIFY(!execute(batch,&output));QVERIFY(collision.open(QIODevice::ReadOnly));QCOMPARE(collision.readAll(),QByteArray("keep collision"));collision.close();
        QVERIFY2(execute(RemoteFiles::sftpRemoveBatch(uploaded,false),&output),output.constData());
        QVERIFY(execute(batch,&output));QVERIFY(collision.open(QIODevice::ReadOnly));QCOMPARE(collision.readAll(),bytes);collision.close();
        QVERIFY(execute(RemoteFiles::sftpRemoveBatch(uploaded,false),&output));
        QVERIFY(execute(RemoteFiles::sftpRemoveBatch(uploaded+".safe.part",false),&output)==false);
        QVERIFY(!uploadedFile.exists());QVERIFY(execute(RemoteFiles::sftpRemoveBatch(path+"/upload",true),&output));QVERIFY(!QDir(sandbox.filePath("upload")).exists());
        QVERIFY(RemoteFiles::sftpRemoveBatch("/",true).isEmpty());
    }
    void remoteNamesCannotEscapeWindowsSaveDirectory() {
        QByteArray fixture("SSFM1\0",6);for(const auto &part:QList<QByteArray>{"f","..\\escape.bin","1","f","NUL.txt","1"}){fixture+=part;fixture+='\0';}
        qputenv("SERIALSTUDIO_FAKE_FILE_LIST",fixture.toBase64());
        QTemporaryDir dir;RemoteFiles browser;
        const bool started=browser.browse("adb","/sdcard",{{"adb",QCoreApplication::applicationFilePath()},{"serial","fixture"}});
        QVERIFY(started);QTRY_VERIFY(!browser.busy());qunsetenv("SERIALSTUDIO_FAKE_FILE_LIST");
        QVERIFY(browser.success());QCOMPARE(browser.entries().size(),2);
        for(const auto &row:browser.entries())QVERIFY(!browser.download(row.toMap().value("id").toString(),dir.path()));
        QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    }
    void invalidTargetsAndPermissionFailures() {
        RemoteFiles browser;QVERIFY(!browser.browse("adb","/sdcard",{{"serial","bad\nserial"}}));
        QVERIFY(!browser.busy());QVERIFY(browser.entries().isEmpty());
        QVERIFY(!browser.browse("sftp","/",{{"host","bad;host"},{"user","root"},{"port",22}}));
        QVERIFY(!browser.browse("unknown","/"));
        QTemporaryDir dir;QVERIFY(browser.browse("local",dir.filePath("missing")));QTRY_VERIFY(!browser.busy());
        QVERIFY(!browser.success());QVERIFY(browser.entries().isEmpty());
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    // 测试专用ADB替身：exec-out透传到真实sh，push/pull仅复制临时夹具。
    if(args.size()>3 && args[1]=="-s") {
        if(args[2]!="fixture")return 17;
        if(args[3]=="exec-out" && args.size()>=5) {
            const auto fixture=qgetenv("SERIALSTUDIO_FAKE_FILE_LIST");
            if(!fixture.isEmpty()){QFile out;if(!out.open(stdout,QIODevice::WriteOnly))return 21;out.write(QByteArray::fromBase64(fixture));return 0;}
            // 与实际 platform-tools 37 保持一致：exec-out 保留命令参数，
            // 后续参数分别进行 shell 转义，不能模拟成直接拼接 sh -c 脚本。
            QString command=args[4];
            for(qsizetype i=5;i<args.size();++i)command+=' '+RemoteFiles::shellQuote(args[i]);
            QProcess process;process.start("C:/Program Files/Git/usr/bin/bash.exe",{"-c","export PATH=/usr/bin:/bin; "+command});
            if(!process.waitForFinished(15000))return 18;
            QFile out;if(!out.open(stdout,QIODevice::WriteOnly))return 21;out.write(process.readAllStandardOutput());out.close();
            QFile errors;if(!errors.open(stderr,QIODevice::WriteOnly))return 22;errors.write(process.readAllStandardError());errors.close();
            return process.exitCode();
        }
        auto native=[](QString path){if(path.size()>3 && path[0]=='/' && path[2]=='/')return path.mid(1,1).toUpper()+":"+path.mid(2);return path;};
        if(args.size()==6 && (args[3]=="push" || args[3]=="pull")) {
            const auto target=native(args[5]);if(!QFile::copy(native(args[4]),target))return 19;
            if(args[3]=="push" && qEnvironmentVariableIsSet("SERIALSTUDIO_CORRUPT_PUSH")){QFile corrupt(target);if(!corrupt.open(QIODevice::WriteOnly))return 23;corrupt.write("corrupted");}
            return 0;
        }
        return 20;
    }
    RemoteFilesTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "remotefiles_tests.moc"
