#include "remotefiles.h"
#include "toolpaths.h"
#include "portablefiles.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QUuid>
#include <QCryptographicHash>
#include <algorithm>

namespace {
QString childPath(const QString &parent,const QString &name) {return parent+(parent.endsWith('/')?"":"/")+name;}
bool localFileName(const QString &name) {
    if(name.isEmpty() || name=="." || name==".." || name.endsWith(' ') || name.endsWith('.'))return false;
    for(auto c:name)if(c.unicode()<32 || QStringLiteral("/\\:*?\"<>|").contains(c))return false;
    static const QRegularExpression reserved("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",QRegularExpression::CaseInsensitiveOption);
    return !reserved.match(name).hasMatch();
}
QVariantMap entry(const QString &name,const QString &id,bool directory,qint64 size=0) {
    return {{"name",name},{"id",id},{"directory",directory},{"size",size}};
}
void sortEntries(QVariantList &rows) {
    std::sort(rows.begin(),rows.end(),[](const QVariant &a,const QVariant &b){
        const auto left=a.toMap(),right=b.toMap();
        if(left.value("directory")!=right.value("directory"))return left.value("directory").toBool();
        return QString::localeAwareCompare(left.value("name").toString(),right.value("name").toString())<0;
    });
}
QString sftpPath() {
    return ToolPaths::resolve("sftp");
}
QString sftpQuote(QString path) {
    path.replace('\\',"\\\\");path.replace('"',"\\\"");return '"'+path+'"';
}
struct ProcessResult {QByteArray output;QString error;};
ProcessResult run(const QString &program,const QStringList &args,const QByteArray &input,std::atomic_bool &cancelled,int timeout=20000) {
    QProcess process;ProcessResult result;QElapsedTimer timer;timer.start();
    if(cancelled.load()){result.error=QStringLiteral("已取消");return result;}
    process.start(program,args);
    if(!process.waitForStarted(5000)){result.error=QStringLiteral("工具启动失败：")+process.errorString();return result;}
    process.write(input);process.closeWriteChannel();QByteArray errors;
    while(process.state()!=QProcess::NotRunning) {
        process.waitForFinished(75);result.output+=process.readAllStandardOutput();errors+=process.readAllStandardError();errors=errors.right(65536);
        if(cancelled.load() || timer.elapsed()>timeout || result.output.size()>16*1024*1024) {
            process.kill();process.waitForFinished(2000);
            result.error=cancelled.load()?QStringLiteral("已取消；传输中已完成的写入无法撤回，请检查目标或临时文件"):
                         result.output.size()>16*1024*1024?QStringLiteral("目录输出超过16MiB，请选择更小的目录"):QStringLiteral("访问超时，请检查设备、权限或网络");
            return result;
        }
    }
    result.output+=process.readAllStandardOutput();errors+=process.readAllStandardError();
    if(process.exitCode()!=0 || process.exitStatus()!=QProcess::NormalExit)
        result.error=QStringLiteral("设备访问失败：")+QString::fromUtf8(errors.right(8192))+QString::fromUtf8(result.output.right(4096));
    return result;
}
ProcessResult runSftp(const QVariantMap &options,const QByteArray &batch,std::atomic_bool &cancelled,int timeout=20000) {
    QStringList args;QByteArray unused;QString error;auto config=options;config["remote"]="/";
    if(!FileTransfer::sftpCommands(2,"C:/unused","C:/unused.part",config,&args,&unused,&error))return {{},error};
    return run(sftpPath(),args,batch,cancelled,timeout);
}
QVariantList readDirectory(const QString &kind,const QString &path,const QVariantMap &options,std::atomic_bool &cancelled,QString *error) {
    QVariantList rows;error->clear();
    if(cancelled.load()){*error=QStringLiteral("已取消目录读取");return {};}
    if(kind=="local") {
        if(path.isEmpty()) {for(const auto &volume:QStorageInfo::mountedVolumes())if(volume.isReady())rows.append(entry(volume.displayName(),volume.rootPath(),true));}
        else {QDir directory(path);if(!directory.exists() || !directory.isReadable())*error=QStringLiteral("目录不存在或没有读取权限：")+path;
            else for(const auto &info:directory.entryInfoList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System,QDir::DirsFirst|QDir::Name)) {
                if(!info.isFile() && !info.isDir() && !info.isSymLink())continue;
                auto item=entry(info.fileName(),info.absoluteFilePath(),info.isDir(),info.size());item["link"]=info.isSymLink();rows.append(item);
            }}
    } else if(kind=="mtp") {
        const auto listed=path.isEmpty()?PortableFiles::roots():PortableFiles::list(path);rows=listed.entries;*error=listed.error;
    } else if(kind=="adb") {
        const auto script="cd "+RemoteFiles::shellQuote(path)+" || exit 1; [ -r . ] || { printf '%s\\n' 'directory is not readable' >&2; exit 1; }; printf 'SSFM1\\0'; for f in .[!.]* ..?* *; do [ -e \"$f\" ] || [ -L \"$f\" ] || continue; if [ -L \"$f\" ]; then t=l; elif [ -d \"$f\" ]; then t=d; elif [ -f \"$f\" ]; then t=f; else continue; fi; s=$(stat -c %s -- \"$f\" 2>/dev/null) || s=0; printf '%s\\0%s\\0%s\\0' \"$t\" \"$f\" \"$s\"; done";
        const auto response=run(options.value("adb").toString(),{"-s",options.value("serial").toString(),"exec-out","sh -c "+RemoteFiles::shellQuote(script)}, {},cancelled);
        *error=response.error;if(error->isEmpty())rows=RemoteFiles::parseAdbListing(response.output,path,error);
    } else if(kind=="sftp") {
        const auto response=runSftp(options,RemoteFiles::sftpListBatch(path),cancelled);*error=response.error;
        if(error->isEmpty())rows=RemoteFiles::parseSftpListing(response.output,path,error);
    } else *error=QStringLiteral("未知文件接口");
    sortEntries(rows);return error->isEmpty()?rows:QVariantList();
}
QString eraseEntry(const QString &kind,const QVariantMap &item,const QVariantMap &options,std::atomic_bool &cancelled,int depth=0,int *remaining=nullptr) {
    const auto path=item.value("id").toString();const bool directory=item.value("directory").toBool() && !item.value("link").toBool();
    if(cancelled.load())return QStringLiteral("删除已取消；已删除的内容不能撤回");
    if(!item.value("deletable",true).toBool())return QStringLiteral("项目路径或删除权限无法可靠确认，拒绝删除；已删除的内容不能撤回");
    if(path.isEmpty() || path=="/" || (kind=="local" && QDir(path).isRoot()))return QStringLiteral("拒绝删除存储根目录");
    if(kind=="mtp")return PortableFiles::remove(path,&cancelled);
    if(kind=="local") {
        const bool ok=directory?QDir(path).removeRecursively():QFile::remove(path);
        return ok?QString():QStringLiteral("删除失败或目录未完整删除，请检查权限并刷新目录");
    }
    if(!RemoteFiles::validRemotePath(path) || QDir::cleanPath(path)=="/")return QStringLiteral("拒绝删除无效远端路径");
    if(kind=="adb") {
        const auto script=QString(directory?"rm -rf -- ":"rm -f -- ")+RemoteFiles::shellQuote(path);
        return run(options.value("adb").toString(),{"-s",options.value("serial").toString(),"exec-out","sh -c "+RemoteFiles::shellQuote(script)}, {},cancelled,1800000).error;
    }
    if(kind=="sftp") {
        int budget=10000;if(!remaining)remaining=&budget;
        if(depth>64 || --*remaining<0)return QStringLiteral("目录删除超过64层或10000项，请分批操作；已删除内容不能撤回");
        if(directory) {
            QString error;const auto children=readDirectory(kind,path,options,cancelled,&error);if(!error.isEmpty())return error;
            for(const auto &child:children){error=eraseEntry(kind,child.toMap(),options,cancelled,depth+1,remaining);if(!error.isEmpty())return error;}
        }
        return runSftp(options,RemoteFiles::sftpRemoveBatch(path,directory),cancelled).error;
    }
    return QStringLiteral("此接口不支持删除");
}
QString transferFile(const QString &kind,const QVariantMap &options,bool upload,const QString &local,const QString &remote,std::atomic_bool &cancelled) {
    if(cancelled.load())return QStringLiteral("传输已取消，源文件保留");
    if(kind=="mtp")return PortableFiles::copy(upload?local:remote,upload?remote:QFileInfo(local).absolutePath(),&cancelled);
    if(kind=="local") {
        const auto source=upload?local:remote,target=upload?remote:local;
        if(QFileInfo::exists(target))return QStringLiteral("目标同名文件已存在，拒绝覆盖");
        QFile input(source);if(!input.open(QIODevice::ReadOnly))return input.errorString();
        QTemporaryDir staging(QFileInfo(target).absolutePath()+"/.serialstudio-paste-XXXXXX");
        if(!staging.isValid())return QStringLiteral("无法创建目标临时目录");
        QFile output(staging.filePath("data.part"));if(!output.open(QIODevice::WriteOnly))return output.errorString();
        while(!input.atEnd()) {
            if(cancelled.load())return QStringLiteral("传输已取消，源文件保留");
            const auto chunk=input.read(262144);
            if(input.error()!=QFile::NoError || chunk.isEmpty() || output.write(chunk)!=chunk.size())return QStringLiteral("文件读取或写入失败");
        }
        output.close();input.close();
        if(cancelled.load() || !QFile::rename(output.fileName(),target))return QStringLiteral("传输已取消或目标已存在，未覆盖");
        return {};
    }
    if(kind=="adb") {
        const auto tool=options.value("adb").toString(),serial=options.value("serial").toString();
        if(!upload)return run(tool,{"-s",serial,"pull",remote,local},{},cancelled,1800000).error;
        const auto temporary=remote+".serialstudio-"+QUuid::createUuid().toString(QUuid::Id128)+".part";
        auto error=run(tool,{"-s",serial,"push",local,temporary},{},cancelled,1800000).error;
        if(error.isEmpty()) {
            const auto script="set -C; cat "+RemoteFiles::shellQuote(temporary)+" > "+RemoteFiles::shellQuote(remote)+" || exit 1; rm -f "+RemoteFiles::shellQuote(temporary);
            error=run(tool,{"-s",serial,"exec-out","sh -c "+RemoteFiles::shellQuote(script)},{},cancelled,1800000).error;
        }
        return error.isEmpty()?error:error+QStringLiteral("\n请检查临时文件：")+temporary;
    }
    if(kind=="sftp") {
        auto config=options;config["remote"]=remote;config["overwrite"]=true;config["noClobber"]=true;
        const auto temporary=upload?remote+".serialstudio-"+QUuid::createUuid().toString(QUuid::Id128)+".part":local;
        QStringList args;QByteArray batch;QString error;
        if(!FileTransfer::sftpCommands(upload?1:2,local,temporary,config,&args,&batch,&error))return error;
        error=run(sftpPath(),args,batch,cancelled,1800000).error;
        return error.isEmpty()?error:error+QStringLiteral("\n请检查传输临时文件：")+temporary;
    }
    return QStringLiteral("不支持的文件接口");
}
QByteArray fileDigest(const QString &path,std::atomic_bool &cancelled,QString *error) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly)){*error=file.errorString();return {};}
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd()) {
        if(cancelled.load()){*error=QStringLiteral("校验已取消，源文件保留");return {};}
        const auto bytes=file.read(262144);if(file.error()!=QFile::NoError || bytes.isEmpty()){*error=QStringLiteral("文件校验读取失败");return {};}
        hash.addData(bytes);
    }
    return hash.result();
}
}

QString RemoteFiles::shellQuote(const QString &value) {QString s=value;s.replace('\'',"'\"'\"'");return '\''+s+'\'';}
QByteArray RemoteFiles::sftpListBatch(const QString &path) {return ("cd "+sftpQuote(path)+"\nls -lna\n").toUtf8();}
QByteArray RemoteFiles::sftpRemoveBatch(const QString &path,bool directory) {
    if(!validRemotePath(path) || QDir::cleanPath(path)=="/")return {};
    return ((directory?"rmdir ":"rm ")+sftpQuote(path)+"\n").toUtf8();
}
bool RemoteFiles::validRemotePath(const QString &path) {
    if(path.size()>4096 || !path.startsWith('/'))return false;
    for(const auto c:path)if(c.unicode()<32 || c.unicode()==127)return false;
    return true;
}
QVariantList RemoteFiles::parseAdbListing(const QByteArray &data,const QString &path,QString *error) {
    error->clear();QVariantList rows;
    if(QString::fromUtf8(data).toUtf8()!=data){*error=QStringLiteral("ADB目录含非UTF-8名称，无法安全表示，已拒绝操作");return {};}
    if(!data.startsWith(QByteArray("SSFM1\0",6))){*error=QStringLiteral("ADB目录返回格式无效，请检查设备授权及sh工具");return {};}
    auto fields=data.mid(6).split('\0');if(!fields.isEmpty() && fields.last().isEmpty())fields.removeLast();
    if(fields.size()%3){*error=QStringLiteral("ADB目录数据不完整");return {};}
    for(qsizetype i=0;i<fields.size();i+=3) {
        const auto name=QString::fromUtf8(fields[i+1]);bool ok=false;const auto size=fields[i+2].toLongLong(&ok);
        if(name=="." || name=="..")continue;
        if(name.isEmpty() || name.contains('/') || !validRemotePath(childPath(path,name)) || !ok || size<0 ||
           (fields[i]!="d" && fields[i]!="f" && fields[i]!="l")) {*error=QStringLiteral("目录包含无法安全表示的名称或类型");return {};}
        auto item=entry(name,childPath(path,name),fields[i]=="d",size);item["link"]=fields[i]=="l";rows.append(item);
    }
    return rows;
}
QVariantList RemoteFiles::parseSftpListing(const QByteArray &data,const QString &path,QString *error) {
    error->clear();QVariantList rows;
    if(QString::fromUtf8(data).toUtf8()!=data){*error=QStringLiteral("SFTP目录含非UTF-8名称，无法安全表示，已拒绝操作");return {};}
    // Windows OpenSSH 用 * 表示服务端未提供的权限位，名称仍按 UTF-8 读取。
    static const QRegularExpression pattern("^([bcdlps-])[rwxStTs*-]{9}\\s+(?:\\d+|\\?)\\s+\\S+\\s+\\S+\\s+(\\d+(?:,\\s*\\d+)?)\\s+\\S+\\s+\\d+\\s+\\S+ (.*)$");
    for(auto line:QString::fromUtf8(data).split('\n')) {
        if(line.endsWith('\r'))line.chop(1);
        if(line.isEmpty() || line.startsWith("sftp> ") || line.startsWith("Connected to ") || line.startsWith("total "))continue;
        const auto match=pattern.match(line);
        if(!match.hasMatch()){*error=QStringLiteral("SFTP目录返回格式无法识别，未将未知输出当作文件：")+line.left(180);return {};}
        auto name=match.captured(3);const auto type=match.captured(1);
        if(type!="d" && type!="l" && type!="-")continue;
        if(name=="." || name=="..")continue;
        // ls 的链接名称与目标之间没有无歧义分隔；保留显示，不推导可执行路径。
        if(type!="l" && (name.isEmpty() || name.contains('/') || !validRemotePath(childPath(path,name)))){*error=QStringLiteral("SFTP目录包含不支持的文件名");return {};}
        bool sizeOk=false;const auto size=match.captured(2).toLongLong(&sizeOk);
        if(!sizeOk){*error=QStringLiteral("SFTP返回的文件大小超出支持范围");return {};}
        auto item=entry(name,type=="l"?QString():childPath(path,name),type=="d",size);item["link"]=type=="l";
        if(type=="l")item["deletable"]=false;
        rows.append(item);
    }
    return rows;
}

RemoteFiles::RemoteFiles(QObject *parent):QObject(parent),m_worker(new RemoteFileWorker) {
    m_worker->moveToThread(&m_thread);connect(&m_thread,&QThread::finished,m_worker,&QObject::deleteLater);
    connect(this,&RemoteFiles::requested,m_worker,&RemoteFileWorker::execute);
    connect(m_worker,&RemoteFileWorker::progress,this,[this](int p){m_progress=p;emit changed();});
    connect(m_worker,&RemoteFileWorker::completed,this,[this](const QVariantMap &result){
        m_busy=false;m_success=result.value("error").toString().isEmpty();
        m_status=m_success?result.value("message",QStringLiteral("目录已读取")).toString():result.value("error").toString();
        if(result.value("action")=="list") {
            m_entries=result.value("entries").toList();m_listed=m_success;
            if(m_success){m_path=result.value("path").toString();m_kind=result.value("kind").toString();m_options=result.value("options").toMap();}
        }
        if(result.contains("refreshed")){m_entries=result.value("entries").toList();m_listed=result.value("refreshed").toBool();}
        if(result.value("cutCommitted").toBool())m_clipboard.clear();
        if(m_success)m_progress=100;
        emit changed();
    });
    connect(&m_transfer,&FileTransfer::changed,this,[this]{
        m_busy=m_transfer.busy();m_success=m_transfer.success();m_status=m_transfer.status();m_progress=m_transfer.progress();emit changed();
    });
    m_thread.start();
}
RemoteFiles::~RemoteFiles(){m_worker->cancelled.store(true);m_thread.quit();m_thread.wait();}
bool RemoteFiles::fail(const QString &message){m_success=false;m_status=message;emit changed();return false;}
bool RemoteFiles::copyEntry(const QString &id,bool cut){
    if(m_busy)return false;
    if(!m_listed || m_path.isEmpty())return fail(QStringLiteral("先打开目录并选择普通文件"));
    for(const auto &value:m_entries){const auto item=value.toMap();if(item.value("id")==id){
        if(item.value("directory").toBool() || item.value("link").toBool())return fail(QStringLiteral("复制/剪切当前支持单个普通文件，目录可进入后逐个操作"));
        if(!localFileName(item.value("name").toString()))return fail(QStringLiteral("此文件名不能安全通过Windows暂存，请先在设备端改名"));
        if(cut && !item.value("deletable",true).toBool())return fail(QStringLiteral("设备没有提供删除权限，不能剪切此文件"));
        m_clipboard={{"kind",m_kind},{"path",m_path},{"options",m_options},{"entry",item},{"cut",cut}};
        m_success=true;m_status=QStringLiteral("已%1：%2；进入目标目录后粘贴，当前未改动源文件。").arg(cut?QStringLiteral("剪切"):QStringLiteral("复制"),clipboardName());emit changed();return true;
    }}
    return fail(QStringLiteral("只能复制/剪切当前目录已列出的普通文件"));
}
bool RemoteFiles::paste(){
    if(m_busy)return false;
    if(m_clipboard.isEmpty() || !m_listed || m_path.isEmpty())return fail(QStringLiteral("先复制或剪切文件，再打开目标目录粘贴"));
    const auto sameOptions=[&]{if(m_kind=="adb")return m_options.value("serial")==m_clipboard.value("options").toMap().value("serial");return m_options==m_clipboard.value("options").toMap();};
    if(m_kind==m_clipboard.value("kind") && m_path==m_clipboard.value("path") && sameOptions())return fail(QStringLiteral("源目录和目标目录相同，无需粘贴"));
    m_status=QStringLiteral("正在粘贴并回读SHA256校验，校验成功后才会删除剪切源文件…");
    return startJob({{"action","paste"},{"kind",m_kind},{"path",m_path},{"options",m_options},{"clipboard",m_clipboard}});
}
bool RemoteFiles::removeEntry(const QString &id,bool confirmed){
    if(m_busy)return false;
    if(!confirmed)return fail(QStringLiteral("删除前需要明确确认，永久删除不能撤回"));
    if(!m_listed || m_path.isEmpty())return fail(QStringLiteral("先打开普通目录，不能删除设备或存储根入口"));
    for(const auto &value:m_entries){const auto item=value.toMap();if(item.value("id")==id){
        if(!item.value("deletable",true).toBool())return fail(QStringLiteral("设备没有提供此项目的删除权限"));
        m_status=QStringLiteral("正在删除，已经删除的内容不能撤回…");
        return startJob({{"action","delete"},{"kind",m_kind},{"path",m_path},{"options",m_options},{"entry",item}});
    }}
    return fail(QStringLiteral("只能删除当前目录中已列出的项目"));
}
void RemoteFiles::clearClipboard(){m_clipboard.clear();emit changed();}
void RemoteFiles::reset(){if(m_busy)return;m_entries.clear();m_listed=false;m_path.clear();m_kind.clear();m_options.clear();m_success=true;m_status=QStringLiteral("目标已改变，点击打开目录。");emit changed();}
bool RemoteFiles::startJob(QVariantMap job){if(m_busy)return false;m_worker->cancelled.store(false);m_busy=true;m_success=true;m_progress=0;emit changed();emit requested(job);return true;}
bool RemoteFiles::browse(const QString &kind,const QString &path,const QVariantMap &options) {
    if(m_busy)return false;
    m_entries.clear();m_listed=false;m_path.clear();m_kind.clear();m_options.clear();
    QVariantMap actual=options;
    if(kind=="adb") {
        const auto serial=options.value("serial").toString();
        if(!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,255}$").match(serial).hasMatch() || !validRemotePath(path))return fail(QStringLiteral("请选择已授权ADB设备和绝对目录"));
        const auto tool=AdbDiscovery::resolveExecutable(options.value("adb").toString());
        if(tool.isEmpty())return fail(QStringLiteral("未找到ADB工具，请在高级设置指定platform-tools/adb.exe"));
        actual["adb"]=tool;
    } else if(kind=="sftp") {
        QStringList arguments;QByteArray batch;QString error;actual["remote"]=path;
        if(!validRemotePath(path) || !FileTransfer::sftpCommands(2,"C:/unused","C:/unused.part",actual,&arguments,&batch,&error))return fail(error.isEmpty()?QStringLiteral("请输入SFTP绝对目录"):error);
        if(sftpPath().isEmpty())return fail(QStringLiteral("未安装Windows OpenSSH SFTP客户端"));
    } else if(kind!="local" && kind!="mtp")return fail(QStringLiteral("不支持的文件接口；选择本地/共享、ADB、MTP或SFTP"));
    if(path.contains(QChar::Null))return fail(QStringLiteral("路径包含NUL字符"));
    m_entries.clear();m_listed=false;m_status=QStringLiteral("正在读取目录…");
    return startJob({{"action","list"},{"kind",kind},{"path",path},{"options",actual}});
}
bool RemoteFiles::upload(const QString &localFile,bool allowOverwrite) {
    if(m_busy)return false;
    if(!m_listed || m_path.isEmpty() || !QFileInfo(localFile).isFile())return fail(QStringLiteral("先打开目标目录，再选择有效的本地文件"));
    if((m_kind=="adb" || m_kind=="sftp") && !validRemotePath(childPath(m_path,QFileInfo(localFile).fileName())))
        return fail(QStringLiteral("远端文件名包含不支持的控制字符"));
    return startTransfer(true,localFile,m_kind=="mtp"?m_path:childPath(m_path,QFileInfo(localFile).fileName()),allowOverwrite);
}
bool RemoteFiles::download(const QString &entryId,const QString &directory) {
    if(m_busy)return false;
    if(!m_listed || !QFileInfo(directory).isDir())return fail(QStringLiteral("先打开目录并选择本地保存文件夹"));
    for(const auto &value:m_entries){const auto item=value.toMap();if(item.value("id")==entryId && !item.value("directory").toBool() && !item.value("link").toBool()) {
        if(!localFileName(item.value("name").toString()))return fail(QStringLiteral("该文件名无法安全保存到Windows，请在设备端改名后再下载"));
        return startTransfer(false,QDir(directory).filePath(item.value("name").toString()),entryId);
    }}
    return fail(QStringLiteral("请选择当前目录中的普通文件；目录和符号链接不执行递归下载"));
}
bool RemoteFiles::startTransfer(bool upload,const QString &local,const QString &remote,bool overwrite) {
    if(m_kind=="local")return m_transfer.start(0,QUrl::fromLocalFile(upload?local:remote),QUrl::fromLocalFile(upload?m_path:QFileInfo(local).absolutePath()),{});
    if(m_kind=="sftp") {
        auto options=m_options;options["remote"]=remote;options["overwrite"]=overwrite;
        return m_transfer.start(upload?1:2,QUrl::fromLocalFile(local),QUrl::fromLocalFile(local),options);
    }
    if(!upload && QFileInfo::exists(local))return fail(QStringLiteral("本地目标同名文件已存在，未覆盖"));
    m_status=QStringLiteral("正在传输；取消或断线可能留下部分文件，请检查目标…");
    return startJob({{"action",upload?"upload":"download"},{"kind",m_kind},{"local",local},{"remote",remote},{"options",m_options}});
}
void RemoteFiles::cancel(){if(!m_busy)return;m_worker->cancelled.store(true);m_transfer.cancel();m_status=QStringLiteral("正在请求取消；已写入的内容无法撤回");emit changed();}

void RemoteFileWorker::execute(const QVariantMap &job) {
    QVariantMap result=job;QString error;QVariantList rows;
    const auto kind=job.value("kind").toString(),action=job.value("action").toString(),path=job.value("path").toString();
    const auto options=job.value("options").toMap();
    if(action=="list") {
        rows=readDirectory(kind,path,options,cancelled,&error);result["entries"]=rows;
        result["message"]=QStringLiteral("已读取 %1 项；双击目录进入，选择文件后下载。").arg(rows.size());
    } else if(action=="delete") {
        error=eraseEntry(kind,job.value("entry").toMap(),options,cancelled);
        result["message"]=QStringLiteral("项目已永久删除。");
    } else if(action=="paste") {
        const auto clipboard=job.value("clipboard").toMap(),source=clipboard.value("entry").toMap();
        const auto sourceKind=clipboard.value("kind").toString(),name=source.value("name").toString();
        const auto sourceOptions=clipboard.value("options").toMap();
        const bool cut=clipboard.value("cut").toBool();
        auto performPaste=[&]() -> QString {
            QString issue;const auto existing=readDirectory(kind,path,options,cancelled,&issue);
            if(!issue.isEmpty())return issue;
            for(const auto &value:existing)if(QString::compare(value.toMap().value("name").toString(),name,
                    kind=="local" || kind=="mtp"?Qt::CaseInsensitive:Qt::CaseSensitive)==0)
                return QStringLiteral("目标同名项目已存在，未覆盖，源文件保留");
            QTemporaryDir staging;if(!staging.isValid())return QStringLiteral("无法创建文件校验暂存目录");
            for(const auto &folder:QStringList{"source","target","current"})if(!QDir().mkpath(staging.filePath(folder)))return QStringLiteral("无法创建校验目录");
            const auto staged=staging.filePath("source/"+name);
            issue=transferFile(sourceKind,sourceOptions,false,staged,source.value("id").toString(),cancelled);
            if(!issue.isEmpty())return issue;
            const auto expected=fileDigest(staged,cancelled,&issue);if(!issue.isEmpty())return issue;
            emit progress(20);
            issue=transferFile(kind,options,true,staged,kind=="mtp"?path:childPath(path,name),cancelled);
            if(!issue.isEmpty())return issue;
            emit progress(50);
            const auto listing=readDirectory(kind,path,options,cancelled,&issue);if(!issue.isEmpty())return issue;
            QVariantMap target;
            for(const auto &value:listing)if(value.toMap().value("name").toString()==name)target=value.toMap();
            if(target.isEmpty() || target.value("directory").toBool() || target.value("link").toBool())return QStringLiteral("目标文件未找到或类型改变，源文件保留");
            const auto verified=staging.filePath("target/"+name);
            issue=transferFile(kind,options,false,verified,target.value("id").toString(),cancelled);
            if(!issue.isEmpty())return issue;
            const auto actual=fileDigest(verified,cancelled,&issue);if(!issue.isEmpty())return issue;
            if(expected!=actual)return QStringLiteral("目标回读SHA256不一致，源文件保留；请检查目标文件");
            emit progress(80);
            if(cut) {
                const auto current=staging.filePath("current/"+name);
                issue=transferFile(sourceKind,sourceOptions,false,current,source.value("id").toString(),cancelled);
                if(!issue.isEmpty())return QStringLiteral("目标已复制，但源文件无法复验，未删除：")+issue;
                const auto currentHash=fileDigest(current,cancelled,&issue);if(!issue.isEmpty())return issue;
                if(currentHash!=expected)return QStringLiteral("源文件在剪切期间变化，目标已复制，未删除源文件");
                issue=eraseEntry(sourceKind,source,sourceOptions,cancelled);
                if(!issue.isEmpty())return QStringLiteral("目标校验通过，但源文件未能删除：")+issue;
                result["cutCommitted"]=true;
            }
            result["message"]=QStringLiteral("%1完成 · 目标回读SHA256：%2").arg(cut?QStringLiteral("剪切粘贴"):QStringLiteral("复制粘贴"),QString::fromLatin1(expected.toHex()));
            return {};
        };
        error=performPaste();
    } else if(kind=="mtp") {
        const auto source=job.value(action=="upload"?"local":"remote").toString();
        const auto destination=action=="upload"?job.value("remote").toString():QFileInfo(job.value("local").toString()).absolutePath();
        error=PortableFiles::copy(source,destination,&cancelled,[this](int p){emit progress(p);});
    } else if(kind=="adb") {
        const auto tool=options.value("adb").toString(),serial=options.value("serial").toString();
        const auto local=job.value("local").toString(),remote=job.value("remote").toString();
        if(action=="download") {
            QTemporaryDir staging(QFileInfo(local).absolutePath()+"/.serialstudio-adb-XXXXXX");
            if(!staging.isValid())error=QStringLiteral("无法创建下载临时目录");
            else {const auto response=run(tool,{"-s",serial,"pull",remote,staging.filePath("data.part")},{},cancelled,1800000);error=response.error;
                if(error.isEmpty() && !QFile::rename(staging.filePath("data.part"),local))error=QStringLiteral("无法提交下载，目标可能已存在；未覆盖已有文件");}
        } else {
            const auto temporary=remote+".serialstudio-"+QUuid::createUuid().toString(QUuid::Id128)+".part";
            const auto pushed=run(tool,{"-s",serial,"push",local,temporary},{},cancelled,1800000);error=pushed.error;
            if(error.isEmpty()) {
                // noclobber 由远端 shell 原子创建最终文件；不使用会覆盖的 mv。
                const auto script="set -C; cat "+RemoteFiles::shellQuote(temporary)+" > "+RemoteFiles::shellQuote(remote)+" || exit 1; rm -f "+RemoteFiles::shellQuote(temporary);
                const auto committed=run(tool,{"-s",serial,"exec-out","sh -c "+RemoteFiles::shellQuote(script)},{},cancelled,1800000);error=committed.error;
            }
            if(!error.isEmpty())error+=QStringLiteral("\n请检查目标及本次临时文件：")+temporary;
        }
    } else error=QStringLiteral("不支持的文件操作");
    if(cancelled.load() && error.isEmpty())error=QStringLiteral("已请求取消，请检查已完成或部分写入的目标");
    if(action=="delete" || action=="paste") {
        QString refreshError;const auto current=readDirectory(kind,path,options,cancelled,&refreshError);
        result["entries"]=current;result["refreshed"]=refreshError.isEmpty();
        if(!refreshError.isEmpty())error+=QStringLiteral("\n目录刷新失败：")+refreshError;
    } else if(action!="list")result["message"]=QStringLiteral("传输完成；未另做远端SHA256比对，点击刷新查看目录。");
    result["error"]=error;emit completed(result);
}
