#include "filetransfer.h"
#include <QCryptographicHash>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>

void CopyWorker::copy(const QString &source,const QString &directory)
{
    auto fail=[this](const QString &s){emit done(false,s,QUrl());};
    QFile input(source);const QFileInfo info(source);
    const auto target=QDir(directory).filePath(info.fileName());
    if(!info.isFile() || !QFileInfo(directory).isDir() || QFileInfo::exists(target)) {
        fail(QStringLiteral("来源必须是文件、目标必须是目录，且目标同名文件不能已存在"));return;
    }
    if(!input.open(QIODevice::ReadOnly)) {fail(input.errorString());return;}
    QTemporaryDir staging(QDir(directory).filePath(".serialstudio-copy-XXXXXX"));
    if(!staging.isValid()){fail(QStringLiteral("无法创建复制临时目录"));return;}
    const auto staged=staging.filePath("data.part");
    QSaveFile output(staged);output.setDirectWriteFallback(false);
    if(!output.open(QIODevice::WriteOnly)) {fail(output.errorString());return;}
    QCryptographicHash sourceHash(QCryptographicHash::Sha256);qint64 copied=0;int last=-1;
    while(!input.atEnd()) {
        if(cancelled.load()) {fail(QStringLiteral("已取消，临时复制内容已撤销"));return;}
        const auto chunk=input.read(262144);
        if(input.error()!=QFile::NoError || chunk.isEmpty() || output.write(chunk)!=chunk.size()) {fail(QStringLiteral("读取/写入失败，请检查空间与磁盘状态"));return;}
        sourceHash.addData(chunk);copied+=chunk.size();
        const int percent=info.size()>0 ? int(double(copied)/double(info.size())*90):90;
        if(percent!=last){last=percent;emit progress(percent);}
    }
    if(cancelled.load()) {fail(QStringLiteral("已取消，临时复制内容已撤销"));return;}
    const QFileInfo current(source);
    if(copied!=info.size() || current.size()!=info.size() || current.lastModified()!=info.lastModified() || QFileInfo::exists(target)) {
        fail(QStringLiteral("来源在复制期间改变或目标文件已出现，未提交复制"));return;
    }
    input.close();
    if(!output.commit()) {fail(output.errorString());return;}
    if(!QFile::rename(staged,target)){fail(QStringLiteral("目标已存在或无法提交，未覆盖已有文件"));return;}
    QFile verify(target);QCryptographicHash targetHash(QCryptographicHash::Sha256);qint64 checked=0;
    if(!verify.open(QIODevice::ReadOnly)) {fail(QStringLiteral("复制已落盘，但无法重新读取校验：")+target);return;}
    while(!verify.atEnd()) {
        if(cancelled.load()) {fail(QStringLiteral("复制已落盘，校验被取消；请保留并检查：")+target);return;}
        const auto chunk=verify.read(262144);
        if(verify.error()!=QFile::NoError || chunk.isEmpty()) {fail(QStringLiteral("复制已落盘，但校验读取失败：")+target);return;}
        targetHash.addData(chunk);checked+=chunk.size();
        const int percent=copied>0 ? 90+int(double(checked)/double(copied)*10):100;
        if(percent!=last){last=percent;emit progress(percent);}
    }
    if(checked!=copied || sourceHash.result()!=targetHash.result()) {fail(QStringLiteral("SHA256 不一致，请检查目标文件：")+target);return;}
    emit progress(100);
    emit done(true,QStringLiteral("复制完成 · %1 字节\nSHA256：%2\n%3").arg(copied).arg(QString::fromLatin1(targetHash.result().toHex()),target),QUrl::fromLocalFile(target));
}
namespace {
QString sftpProgram()
{
#ifdef Q_OS_WIN
    const auto path=QDir(qEnvironmentVariable("SystemRoot","C:/Windows")).filePath("System32/OpenSSH/sftp.exe");
    if(QFileInfo::exists(path))return path;
#endif
    return QStandardPaths::findExecutable("sftp");
}
bool safePath(const QString &path)
{if(path.isEmpty() || path.size()>2048)return false;for(auto c:path)if(c.unicode()<32 || c==QChar(127))return false;return true;}
QString quoted(QString path)
{path.replace('\\',"\\\\");path.replace('"',"\\\"");return '"'+path+'"';}
QString globSource(const QString &path)
{
    QString literal;
    for(auto c:path){if(QStringLiteral("\\[]*?").contains(c))literal+='\\';literal+=c;}
    return quoted(literal);
}
}
FileTransfer::FileTransfer(QObject *parent):QObject(parent),m_copy(new CopyWorker)
{
    m_copy->moveToThread(&m_thread);connect(&m_thread,&QThread::finished,m_copy,&QObject::deleteLater);
    connect(this,&FileTransfer::copyRequested,m_copy,&CopyWorker::copy);
    connect(m_copy,&CopyWorker::progress,this,[this](int p){m_progress=p;emit changed();});
    connect(m_copy,&CopyWorker::done,this,[this](bool ok,const QString &s,const QUrl &u){finish(ok,s,u);});
    m_thread.start();m_timeout.setSingleShot(true);
    connect(&m_timeout,&QTimer::timeout,this,[this]{m_abort=QStringLiteral("SFTP 超时（30 分钟）；检查远端 part 文件");m_process.kill();});
    auto read=[this]{m_log+=m_process.readAllStandardOutput();m_log+=m_process.readAllStandardError();m_log=m_log.right(32768);};
    connect(&m_process,&QProcess::readyReadStandardError,this,read);connect(&m_process,&QProcess::readyReadStandardOutput,this,read);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart)finish(false,QStringLiteral("SFTP 无法启动：")+m_process.errorString());});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,&FileTransfer::processFinished);
    refreshVolumes();
}
FileTransfer::~FileTransfer()
{
    m_copy->cancelled.store(true);m_thread.quit();m_thread.wait();
    m_process.disconnect(this);if(m_process.state()!=QProcess::NotRunning){m_process.kill();m_process.waitForFinished(1500);}
}
void FileTransfer::refreshVolumes()
{
    m_volumes.clear();
    for(const auto &v:QStorageInfo::mountedVolumes())if(v.isReady() && !v.isReadOnly())
        m_volumes.append(QVariantMap{{"label",QStringLiteral("%1  %2 · 可用 %3 GiB").arg(v.rootPath(),v.displayName()).arg(double(v.bytesAvailable())/1073741824,0,'f',1)},
                                     {"path",v.rootPath()}});
    emit changed();
}
bool FileTransfer::finish(bool success,const QString &message,const QUrl &result)
{m_timeout.stop();m_busy=false;m_success=success;m_status=message;m_result=result;if(success)m_progress=100;m_temp.reset();emit changed();return success;}
bool FileTransfer::sftpCommands(int mode,const QString &local,const QString &temporary,const QVariantMap &o,
                               QStringList *arguments,QByteArray *batch,QString *error)
{
    error->clear();arguments->clear();batch->clear();
    const auto host=o.value("host").toString().trimmed(),user=o.value("user").toString().trimmed(),key=o.value("key").toString().trimmed(),remote=o.value("remote").toString();
    bool ok=false;const int port=o.value("port",22).toInt(&ok);
    if((mode!=1 && mode!=2) || !QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,252}$").match(host).hasMatch()
       || !QRegularExpression("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}$").match(user).hasMatch()
       || !ok || port<1 || port>65535 || (!key.isEmpty() && !QFileInfo(key).isFile())
       || !safePath(local) || !safePath(remote) || !remote.startsWith('/') || !safePath(temporary)) {
        *error=QStringLiteral("检查 SSH 主机、用户、端口、密钥与绝对远端路径；路径不允许换行/控制字符");return false;
    }
    if(mode==1 && !o.value("overwrite").toBool()) {*error=QStringLiteral("上传前请明确勾选允许写入/覆盖远端文件");return false;}
    *arguments={"-b","-","-P",QString::number(port),"-o","BatchMode=yes","-o","StrictHostKeyChecking=yes","-o","ConnectTimeout=10"};
    if(!key.isEmpty())*arguments<<"-i"<<QDir::toNativeSeparators(key);
    *arguments<<"--"<<user+"@"+host;
    if(mode==1)*batch=("put "+globSource(local)+' '+quoted(temporary)+"\nrename "+quoted(temporary)+' '+quoted(remote)+"\n").toUtf8();
    else *batch=("get "+globSource(remote)+' '+quoted(temporary)+"\n").toUtf8();
    return true;
}
bool FileTransfer::start(int mode,const QUrl &source,const QUrl &destination,const QVariantMap &options)
{
    if(m_busy)return false;
    m_progress=0;m_result=QUrl();m_abort.clear();m_log.clear();m_mode=mode;
    if(mode==0) {
        if(!source.isLocalFile() || !destination.isLocalFile())return finish(false,QStringLiteral("请选择本地文件和 Windows 可见的磁盘目录"));
        m_copy->cancelled.store(false);m_busy=true;m_success=true;m_status=QStringLiteral("正在复制并校验，目标已有同名文件将拒绝覆盖…");emit changed();
        emit copyRequested(source.toLocalFile(),destination.toLocalFile());return true;
    }
    if(mode!=1 && mode!=2)return finish(false,QStringLiteral("未知传输方式"));
    if(sftpProgram().isEmpty())return finish(false,QStringLiteral("缺少 OpenSSH SFTP 客户端，请启用 Windows OpenSSH"));
    QString local;
    if(mode==1) {
        if(!source.isLocalFile() || !QFileInfo(source.toLocalFile()).isFile())return finish(false,QStringLiteral("请选择要上传的本地文件"));
        local=QDir::fromNativeSeparators(source.toLocalFile());
        m_partial=options.value("remote").toString()+"."+QUuid::createUuid().toString(QUuid::Id128)+".part";
    } else {
        if(!destination.isLocalFile() || QFileInfo::exists(destination.toLocalFile()) || !QFileInfo(QFileInfo(destination.toLocalFile()).absolutePath()).isDir())
            return finish(false,QStringLiteral("下载目标须为尚不存在的本地文件，父目录必须存在"));
        m_final=destination.toLocalFile();local=m_final;
        m_temp=std::make_unique<QTemporaryDir>(QFileInfo(m_final).absolutePath()+"/.serialstudio-XXXXXX");
        if(!m_temp->isValid())return finish(false,QStringLiteral("无法创建下载临时目录"));
        m_partial=QDir::fromNativeSeparators(m_temp->filePath("download.part"));
    }
    QStringList arguments;QByteArray batch;QString error;
    if(!sftpCommands(mode,local,m_partial,options,&arguments,&batch,&error))return finish(false,error);
    m_busy=true;m_success=true;m_status=QStringLiteral("SFTP 传输中（完成前无百分比）…\n临时文件：%1").arg(m_partial);emit changed();
    m_process.setProgram(sftpProgram());m_process.setArguments(arguments);m_process.start();m_process.write(batch);m_process.closeWriteChannel();m_timeout.start(1800000);return true;
}
void FileTransfer::processFinished(int code,QProcess::ExitStatus state)
{
    if(!m_busy)return;
    m_log+=m_process.readAllStandardOutput();m_log+=m_process.readAllStandardError();
    if(!m_abort.isEmpty() || code!=0 || state!=QProcess::NormalExit) {
        finish(false,(m_abort.isEmpty()?QStringLiteral("SFTP 失败："):m_abort+'\n')+QString::fromUtf8(m_log.right(16384))
               +(m_mode==1?QStringLiteral("\n远端可能留有临时文件：")+m_partial:QString()));return;
    }
    if(m_mode==2) {
        if(!QFile::rename(m_partial,m_final)) {finish(false,QStringLiteral("下载完成但本地落盘失败或目标已存在，未覆盖；临时文件将清理"));return;}
        finish(true,QStringLiteral("SFTP 下载完成：%1\n已由 SSH 加密传输；未另做远端 SHA256 比对。").arg(m_final),QUrl::fromLocalFile(m_final));
    } else finish(true,QStringLiteral("SFTP 上传完成；远端文件已提交。SSH 加密传输，未另做远端 SHA256 比对。"));
}
void FileTransfer::cancel()
{
    if(!m_busy)return;
    m_status=QStringLiteral("正在取消…");emit changed();
    if(m_mode==0)m_copy->cancelled.store(true);
    else {m_abort=QStringLiteral("已取消 SFTP；已完成的远端操作无法撤回");m_process.kill();}
}
