#include "commandcatalog.h"
#include "updatecontroller.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace {
bool noLinks(const QString &path) {
    for(QFileInfo p(path);;p=QFileInfo(p.absolutePath())) {
        if(p.isSymLink()||p.isJunction())return false;
        if(p.absoluteFilePath()==p.absolutePath())return true;
    }
}
QByteArray readFile(const QString &path) {
    QFile f(path);
    if(!noLinks(path)||!f.open(QIODevice::ReadOnly)||f.size()>CommandCatalog::maximumBytes)return {};
    return f.read(CommandCatalog::maximumBytes+1);
}
bool save(const QString &path,const QByteArray &bytes) {
    if(!noLinks(path))return false;
    QSaveFile f(path);f.setDirectWriteFallback(false);
    return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();
}
bool text(const QJsonValue &v,bool singleLine=false) {
    if(!v.isString()||v.toString().isEmpty()||v.toString().size()>(singleLine?4096:32768))return false;
    const auto s=v.toString();
    for(const auto c:s)if(c.unicode()==0||(singleLine&&(c.unicode()<32||c.unicode()==127||c.unicode()==0x2028||c.unicode()==0x2029)))return false;
    return true;
}
}
CommandCatalog::CommandCatalog(QString appVersion,QString root,QString seed,QObject *parent)
    :QObject(parent),m_appVersion(std::move(appVersion)),
      m_root(root.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/catalog":QDir(root).absolutePath()),
      m_seed(seed.isEmpty()?QCoreApplication::applicationDirPath()+"/catalog/terminal_commands.json":std::move(seed)) {reload();}
bool CommandCatalog::validVersion(const QString &version) {
    static const QRegularExpression re("^(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})(?:-(alpha|beta|rc)\\.(0|[1-9][0-9]{0,8}))?$");
    return re.match(version).hasMatch();
}
bool CommandCatalog::validate(const QByteArray &bytes,const QString &appVersion,QString *error) {
    auto reject=[error](const QString &message){if(error)*error=message;return false;};
    if(bytes.isEmpty()||bytes.size()>maximumBytes)return reject(QStringLiteral("指令库为空或超过 16 MiB"));
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(bytes,&parse);
    if(parse.error!=QJsonParseError::NoError||!doc.isObject())return reject(QStringLiteral("指令库必须为有效的版本封装 JSON"));
    const auto object=doc.object();
    if(object.value("schemaVersion")!=QJsonValue(1)||!validVersion(object.value("catalogVersion").toString())||!validVersion(object.value("minAppVersion").toString()))
        return reject(QStringLiteral("指令库格式或版本无效"));
    if(UpdateController::compareVersions(object.value("minAppVersion").toString(),appVersion)>0)return reject(QStringLiteral("此指令库需要更高版本 APP：%1").arg(object.value("minAppVersion").toString()));
    if(!object.value("commands").isArray()||object.value("commands").toArray().isEmpty()||object.value("commands").toArray().size()>20000)
        return reject(QStringLiteral("指令列表为空、格式错误或超过 20000 条"));
    QSet<QString> ids;const QStringList systems{"linux","powershell","cmd","android"};
    static const QRegularExpression idPattern("^(linux|powershell|cmd|android)-[a-z0-9-]+$");
    for(const auto &value:object.value("commands").toArray()) {
        if(!value.isObject())return reject(QStringLiteral("指令条目必须为对象"));
        const auto e=value.toObject();const auto id=e.value("id").toString(),system=e.value("system").toString();
        if(!systems.contains(system)||!id.startsWith(system+'-')||!idPattern.match(id).hasMatch()||ids.contains(id))return reject(QStringLiteral("指令系统/标识错误或重复：%1").arg(id));
        ids.insert(id);
        for(const auto &field:{"title","category","description"})if(!text(e.value(field)))return reject(QStringLiteral("指令说明字段无效：%1").arg(id));
        for(const auto &field:{"syntax","example"})if(!text(e.value(field),true))return reject(QStringLiteral("指令必须为有效单行文本：%1").arg(id));
        for(const auto &field:{"requires","usage","location","delivery"})
            if(e.contains(field)&&!text(e.value(field)))return reject(QStringLiteral("指令说明类型无效：%1").arg(id));
        const auto interaction=e.value("interaction").toString();
        if(interaction!="shell"&&interaction!="editor-key")return reject(QStringLiteral("未知交互类型：%1").arg(id));
        if(interaction=="editor-key"&&!QStringList{":wq",":q!","Esc","Ctrl+O","Ctrl+X"}.contains(e.value("key").toString()))return reject(QStringLiteral("未知编辑器按键：%1").arg(id));
        if(e.contains("tags")) {
            if(!e.value("tags").isArray())return reject(QStringLiteral("标签格式错误"));
            for(const auto &tag:e.value("tags").toArray())if(!text(tag))return reject(QStringLiteral("标签必须为文本"));
        }
        for(const auto &field:{"parameters","options","advanced"}) {
            if(!e.contains(field))continue;
            if(!e.value(field).isArray())return reject(QStringLiteral("参数/选项/示例必须为数组：%1").arg(id));
            for(const auto &part:e.value(field).toArray()) {
                if(!part.isObject())return reject(QStringLiteral("参数/示例格式无效"));
                const auto p=part.toObject();
                if(QString(field)=="parameters") {
                    const auto index=p.value("index").toDouble(-1);
                    if(index<0||index>256||index!=int(index)||!text(p.value("label"))||!text(p.value("hint"))||!p.value("choices").isArray())return reject(QStringLiteral("位置参数说明无效"));
                    for(const auto &choice:p.value("choices").toArray())if(!choice.isObject()||!text(choice.toObject().value("value"),true)||!text(choice.toObject().value("description")))return reject(QStringLiteral("候选值说明无效"));
                } else if(QString(field)=="options") {
                    if(!text(p.value("value"),true)||!text(p.value("description")))return reject(QStringLiteral("选项说明无效"));
                } else if(!text(p.value("title"))||!text(p.value("description"))||!text(p.value("command"),true))return reject(QStringLiteral("高级示例无效"));
            }
        }
    }
    return true;
}
bool CommandCatalog::fail(const QString &message){m_status=message;emit changed();return false;}
void CommandCatalog::adopt(const QByteArray &bytes,const QString &source) {
    const auto object=QJsonDocument::fromJson(bytes).object();
    m_bytes=bytes;m_version=object.value("catalogVersion").toString();m_source=source;
    m_entries=object.value("commands").toArray().toVariantList();emit commandsChanged();
}
bool CommandCatalog::reload() {
    QString errors;
    for(const auto &name:QStringList{"current.json","previous.json",m_seed}) {
        const bool seed=name==m_seed;const auto path=seed?m_seed:QDir(m_root).filePath(name);
        if(!QFileInfo::exists(path))continue;
        QString error;const auto bytes=readFile(path);
        if(!validate(bytes,m_appVersion,&error)){errors+=error+"；";continue;}
        adopt(bytes,seed?QStringLiteral("安装目录初始库"):name=="previous.json"?QStringLiteral("上一版本（回退）"):QStringLiteral("用户目录"));
        m_status=errors.isEmpty()?QStringLiteral("已加载 %1 条指令").arg(count()):QStringLiteral("当前库不可用，已回退：%1；%2").arg(m_source,errors);
        emit changed();return true;
    }
    return fail(QStringLiteral("没有可用的外置指令库，请导入 JSON 或修复安装。%1").arg(errors));
}
bool CommandCatalog::hasPrevious() const {return validate(readFile(QDir(m_root).filePath("previous.json")),m_appVersion);}
bool CommandCatalog::apply(const QByteArray &bytes) {
    QString error;if(!validate(bytes,m_appVersion,&error))return fail(error);
    if(!noLinks(m_root)||!QDir().mkpath(m_root))return fail(QStringLiteral("无法创建指令库目录或目录包含链接"));
    if(!m_bytes.isEmpty()&&!save(QDir(m_root).filePath("previous.json"),m_bytes))return fail(QStringLiteral("无法保存上一版本，未替换当前库"));
    if(!save(QDir(m_root).filePath("current.json"),bytes))return fail(QStringLiteral("无法原子写入指令库，当前库保留"));
    adopt(bytes,QStringLiteral("用户目录"));m_status=QStringLiteral("已应用指令库 %1，共 %2 条").arg(m_version).arg(count());emit changed();return true;
}
bool CommandCatalog::importFile(const QUrl &url) {
    if(!url.isLocalFile())return fail(QStringLiteral("请选择本地 JSON 文件"));
    auto bytes=readFile(url.toLocalFile());const auto document=QJsonDocument::fromJson(bytes);
    // 兼容原维护源数组；无版本的手工库显示 0.0.0，后续仍可检查正式库更新。
    if(document.isArray())bytes=QJsonDocument(QJsonObject{{"schemaVersion",1},{"catalogVersion","0.0.0"},{"minAppVersion","1.3.0"},{"commands",document.array()}}).toJson();
    return apply(bytes);
}
bool CommandCatalog::exportFile(const QUrl &url) {
    if(!url.isLocalFile()||m_bytes.isEmpty())return fail(QStringLiteral("请选择本地导出文件，并先加载有效库"));
    const auto path=QFileInfo(url.toLocalFile()).absoluteFilePath();
    for(const auto &protectedPath:QStringList{m_seed,QDir(m_root).filePath("current.json"),QDir(m_root).filePath("previous.json")}) {
#ifdef Q_OS_WIN
        constexpr auto sensitivity=Qt::CaseInsensitive;
#else
        constexpr auto sensitivity=Qt::CaseSensitive;
#endif
        if(path.compare(QFileInfo(protectedPath).absoluteFilePath(),sensitivity)==0)return fail(QStringLiteral("导出不能覆盖指令库工作文件"));
    }
    if(!save(path,m_bytes))return fail(QStringLiteral("无法导出指令库"));
    m_status=QStringLiteral("指令库已导出");emit changed();return true;
}
bool CommandCatalog::rollback() {
    const auto bytes=readFile(QDir(m_root).filePath("previous.json"));return apply(bytes);
}
void CommandCatalog::openDirectory(){if(noLinks(m_root)&&QDir().mkpath(m_root))QDesktopServices::openUrl(QUrl::fromLocalFile(m_root));}
