#include "quickcommands.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>
#include <cmath>

namespace {
const QSet<QString> scopes={"serial","network","http","mqtt","terminal-linux","terminal-powershell",
                           "terminal-cmd","terminal-android","board-i2c","board-spi","board-usb"};
bool validJson(const QVariant &value)
{
    switch(value.metaType().id()) {
    case QMetaType::Nullptr: case QMetaType::Bool: case QMetaType::QString:
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong:return true;
    case QMetaType::Double: case QMetaType::Float:return std::isfinite(value.toDouble());
    case QMetaType::QStringList:return true;
    case QMetaType::QVariantMap:for(const auto &v:value.toMap())if(!validJson(v))return false;return true;
    case QMetaType::QVariantList:for(const auto &v:value.toList())if(!validJson(v))return false;return true;
    default:return false;
    }
}
bool normalize(QVariantMap &entry,bool generateId)
{
    if(!validJson(entry))return false;
    for(const auto &key:{"title","text"})if(entry.value(key).metaType().id()!=QMetaType::QString)return false;
    if(entry.value("title").toString().trimmed().isEmpty())return false;
    for(const auto &key:{"category","newline"}) {
        if(!entry.contains(key))entry.insert(key,QString());
        else if(entry.value(key).metaType().id()!=QMetaType::QString)return false;
    }
    for(const auto &key:{"hex","favorite"}) {
        if(!entry.contains(key))entry.insert(key,false);
        else if(entry.value(key).metaType().id()!=QMetaType::Bool)return false;
    }
    if(!entry.contains("id")&&generateId)entry.insert("id",QUuid::createUuid().toString(QUuid::WithoutBraces));
    if(entry.value("id").metaType().id()!=QMetaType::QString)return false;
    const QUuid uuid(entry.value("id").toString());
    if(uuid.isNull())return false;
    entry.insert("id",uuid.toString(QUuid::WithoutBraces)); return true;
}
}

QuickCommands::QuickCommands(SettingsStore *store,QObject *parent):QObject(parent),m_store(store)
{
    m_timer.setInterval(0); m_timer.setSingleShot(true);
    connect(&m_timer,&QTimer::timeout,this,&QuickCommands::savePending);
    if(store)connect(store,&SettingsStore::aboutToFlush,this,&QuickCommands::savePending);
}

QuickCommands::~QuickCommands() { savePending(); }

bool QuickCommands::fail(const QString &message) const
{
    if(m_error!=message) {m_error=message;emit const_cast<QuickCommands *>(this)->changed();}
    return false;
}

bool QuickCommands::load(const QString &scope) const
{
    if(!scopes.contains(scope))return fail(QStringLiteral("不支持的指令用途：%1").arg(scope));
    if(!m_store)return fail(QStringLiteral("配置仓库不可用"));
    if(!m_store->error().isEmpty())return fail(m_store->error());
    if(m_buckets.contains(scope))return true;
    const auto saved=m_store->value("quickCommands/"+scope,QVariantList{});
    if(saved.metaType().id()!=QMetaType::QVariantList)return fail(QStringLiteral("已保存指令列表格式错误，未覆盖原内容"));
    Bucket bucket;
    for(const auto &item:saved.toList()) {
        if(item.metaType().id()!=QMetaType::QVariantMap)return fail(QStringLiteral("已保存指令格式错误，未覆盖原内容"));
        auto entry=item.toMap();
        if(!normalize(entry,false)||bucket.indices.contains(entry.value("id").toString()))
            return fail(QStringLiteral("已保存指令无效或 ID 重复，未覆盖原内容"));
        bucket.indices.insert(entry.value("id").toString(),bucket.items.size()); bucket.items.append(entry);
    }
    m_buckets.insert(scope,std::move(bucket)); return true;
}

QVariantList QuickCommands::entries(const QString &scope) const
{
    return load(scope)?m_buckets.value(scope).items:QVariantList{};
}

void QuickCommands::schedule(const QString &scope)
{
    // 同一事件轮中的批量新增只刷新一次界面，避免列表重复复制。
    m_pending.insert(scope); m_timer.start(); m_error.clear();
}

void QuickCommands::savePending()
{
    m_timer.stop();
    if(!m_store) {if(!m_pending.isEmpty())fail(QStringLiteral("配置仓库不可用，指令未保存"));return;}
    const auto pending=m_pending;
    for(const auto &scope:pending) {
        if(m_store->setValue("quickCommands/"+scope,m_buckets.value(scope).items))m_pending.remove(scope);
        else fail(m_store->error());
    }
    if(!pending.isEmpty())emit changed();
}

QString QuickCommands::put(const QString &scope,const QVariantMap &entry)
{
    if(!load(scope))return {};
    auto &bucket=m_buckets[scope]; auto merged=entry;
    const auto candidate=QUuid(entry.value("id").toString()).toString(QUuid::WithoutBraces);
    const auto existing=bucket.indices.constFind(candidate);
    if(existing!=bucket.indices.cend()) {
        merged=bucket.items.at(existing.value()).toMap();
        for(auto it=entry.cbegin();it!=entry.cend();++it)merged.insert(it.key(),it.value());
    }
    if(!normalize(merged,true)) {fail(QStringLiteral("指令字段无效：标题、内容、UUID 或元数据格式错误"));return {};}
    const auto id=merged.value("id").toString();
    if(existing!=bucket.indices.cend())bucket.items[existing.value()]=merged;
    else {bucket.indices.insert(id,bucket.items.size());bucket.items.append(merged);}
    schedule(scope); return id;
}

bool QuickCommands::remove(const QString &scope,const QString &id)
{
    if(!load(scope))return false;
    auto &bucket=m_buckets[scope];const auto canonical=QUuid(id).toString(QUuid::WithoutBraces);
    if(!bucket.indices.contains(canonical))return fail(QStringLiteral("没有找到该指令"));
    bucket.items.removeAt(bucket.indices.take(canonical));
    for(qsizetype i=0;i<bucket.items.size();++i)bucket.indices.insert(bucket.items.at(i).toMap().value("id").toString(),i);
    schedule(scope);return true;
}

bool QuickCommands::importFile(const QString &scope,const QUrl &url)
{
    if(!load(scope))return false;
    if(!url.isLocalFile())return fail(QStringLiteral("请选择本地 JSON 文件"));
    QFile file(url.toLocalFile());if(!file.open(QIODevice::ReadOnly))return fail(QStringLiteral("无法读取导入文件：%1").arg(file.errorString()));
    QJsonParseError parse; const auto doc=QJsonDocument::fromJson(file.readAll(),&parse); const auto root=doc.object();
    if(parse.error!=QJsonParseError::NoError||!doc.isObject()||root.value("schemaVersion")!=QJsonValue(1)
       ||root.value("scope").toString()!=scope||!root.value("entries").isArray())
        return fail(QStringLiteral("导入格式、版本或用途不匹配，未导入任何内容"));
    QVariantList normalized; QSet<QString> seen;
    for(const auto &item:root.value("entries").toArray()) {
        if(!item.isObject())return fail(QStringLiteral("导入条目必须为对象，未导入任何内容"));
        auto entry=item.toObject().toVariantMap();
        if(!normalize(entry,true)||seen.contains(entry.value("id").toString()))
            return fail(QStringLiteral("导入条目无效或 ID 重复，未导入任何内容"));
        seen.insert(entry.value("id").toString()); normalized.append(entry);
    }
    // 完整验证之后只更新一次仓库；同 UUID 合并，避免重复导入产生副本。
    auto merged=m_buckets.value(scope);
    for(const auto &item:normalized) {
        auto entry=item.toMap();const auto id=entry.value("id").toString();
        if(merged.indices.contains(id)) {
            auto combined=merged.items.at(merged.indices.value(id)).toMap();
            for(auto it=entry.cbegin();it!=entry.cend();++it)combined.insert(it.key(),it.value());
            merged.items[merged.indices.value(id)]=combined;
        } else {merged.indices.insert(id,merged.items.size());merged.items.append(entry);}
    }
    m_buckets.insert(scope,std::move(merged));schedule(scope);return true;
}

bool QuickCommands::exportFile(const QString &scope,const QUrl &url)
{
    if(!load(scope))return false;
    if(!url.isLocalFile())return fail(QStringLiteral("请选择本地导出文件"));
    QSaveFile file(url.toLocalFile());
    if(!file.open(QIODevice::WriteOnly))return fail(QStringLiteral("无法创建导出文件：%1").arg(file.errorString()));
    const auto bytes=QJsonDocument(QJsonObject{{"schemaVersion",1},{"scope",scope},
                          {"entries",QJsonArray::fromVariantList(m_buckets.value(scope).items)}}).toJson();
    if(file.write(bytes)!=bytes.size()||!file.commit())return fail(QStringLiteral("导出保存失败：%1").arg(file.errorString()));
    if(!m_error.isEmpty()){m_error.clear();emit changed();}return true;
}
