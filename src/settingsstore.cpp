#include "settingsstore.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QJSValue>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {
bool jsonCompatible(const QVariant &value)
{
    switch(value.metaType().id()) {
    case QMetaType::Nullptr: case QMetaType::Bool: case QMetaType::QString:
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong:
        return true;
    case QMetaType::Double: case QMetaType::Float: return std::isfinite(value.toDouble());
    case QMetaType::QStringList: return true;
    case QMetaType::QVariantList:
        for(const auto &item:value.toList())if(!jsonCompatible(item))return false;
        return true;
    case QMetaType::QVariantMap:
        for(const auto &item:value.toMap())if(!jsonCompatible(item))return false;
        return true;
    default:return false;
    }
}
}

SettingsStore::SettingsStore(const QString &directory,QObject *parent):QObject(parent),
    m_directory(directory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation):directory)
{
    m_timer.setSingleShot(true); m_timer.setInterval(250);
    connect(&m_timer,&QTimer::timeout,this,&SettingsStore::flush);
    QFile file(QDir(m_directory).filePath("settings.json"));
    if(file.exists()) {
        if(!file.open(QIODevice::ReadOnly)) {
            m_blocked=true; fail(QStringLiteral("无法读取配置，已阻止覆盖：%1").arg(file.errorString())); return;
        }
        QJsonParseError parse;
        const auto doc=QJsonDocument::fromJson(file.readAll(),&parse);
        const auto root=doc.object();
        if(parse.error!=QJsonParseError::NoError||!doc.isObject()||root.value("schemaVersion")!=QJsonValue(1)
           ||!root.value("values").isObject()||!root.value("secrets").isObject()) {
            m_blocked=true; fail(QStringLiteral("配置损坏或版本不受支持；原文件已保留，已阻止覆盖：%1").arg(file.fileName())); return;
        }
        m_values=root.value("values").toObject(); m_secrets=root.value("secrets").toObject();
        for(const auto &item:m_secrets)if(!item.isString()) {
            m_blocked=true; fail(QStringLiteral("凭据格式损坏，原配置已保留，已阻止覆盖")); return;
        }
    } else {
        m_dirty=true;
        // 显式目录用于隔离验收；只有真实用户配置首次创建时才迁移旧设置。
        if(directory.isEmpty()) {
            const QSettings old;
            for(const auto &key:old.allKeys()) {
                if(key=="dark"||key=="connection"||key=="terminal"||key.startsWith("terminal/")
                   ||key=="adbPath"||key=="updates/ignoredTag") {
                    const auto value=old.value(key);
                    if(jsonCompatible(value))m_values.insert(key,QJsonValue::fromVariant(value));
                }
            }
            m_timer.start();
        }
    }
}

SettingsStore::~SettingsStore() { flush(); }

bool SettingsStore::fail(const QString &message) const
{
    if(m_error!=message) { m_error=message; emit const_cast<SettingsStore *>(this)->changed(); }
    return false;
}

QVariant SettingsStore::value(const QString &key,const QVariant &fallback) const
{
    return m_values.contains(key)?m_values.value(key).toVariant():fallback;
}

bool SettingsStore::setValue(const QString &key,const QVariant &value)
{
    if(m_blocked)return false;
    if(key.trimmed().isEmpty())return fail(QStringLiteral("配置键不能为空"));
    // QML 的对象/数组经 QVariant 参数传入时仍为 QJSValue，先还原 JSON 数据类型。
    const QVariant normalized=value.metaType()==QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
    if(normalized.isValid()&&!jsonCompatible(normalized))return fail(QStringLiteral("配置值必须为有效的 JSON 数据：%1（%2）").arg(key,QString::fromLatin1(normalized.metaType().name())));
    if(!normalized.isValid()) {
        if(!m_values.contains(key))return true;
        m_values.remove(key);
    } else {
        const auto converted=QJsonValue::fromVariant(normalized);
        if(m_values.contains(key)&&m_values.value(key)==converted)return true;
        m_values.insert(key,converted);
    }
    m_dirty=true; m_timer.start(); emit changed(); return true;
}

bool SettingsStore::flush()
{
    emit aboutToFlush();
    if(m_blocked)return false;
    if(!m_dirty)return true;
    m_timer.stop();
    if(m_directory.isEmpty()||!QDir().mkpath(m_directory))return fail(QStringLiteral("无法创建配置目录：%1").arg(m_directory));
    QSaveFile file(QDir(m_directory).filePath("settings.json"));
    if(!file.open(QIODevice::WriteOnly))return fail(QStringLiteral("无法写入配置：%1").arg(file.errorString()));
    const auto bytes=QJsonDocument(QJsonObject{{"schemaVersion",1},{"values",m_values},{"secrets",m_secrets}}).toJson();
    if(file.write(bytes)!=bytes.size()||!file.commit())return fail(QStringLiteral("配置原子保存失败：%1").arg(file.errorString()));
    m_dirty=false;
    if(!m_error.isEmpty()){m_error.clear();emit changed();}
    return true;
}

bool SettingsStore::setSecret(const QString &key,const QString &value)
{
    if(m_blocked)return false;
    if(key.trimmed().isEmpty())return fail(QStringLiteral("凭据键不能为空"));
    if(value.isEmpty())m_secrets.remove(key);
    else {
#ifdef Q_OS_WIN
        const auto bytes=value.toUtf8();
        DATA_BLOB input{static_cast<DWORD>(bytes.size()),reinterpret_cast<BYTE *>(const_cast<char *>(bytes.constData()))};
        DATA_BLOB output{};
        if(!CryptProtectData(&input,L"SerialStudio",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))
            return fail(QStringLiteral("Windows 凭据加密失败：%1").arg(GetLastError()));
        const auto encrypted=QByteArray(reinterpret_cast<const char *>(output.pbData),output.cbData).toBase64();
        LocalFree(output.pbData); m_secrets.insert(key,QString::fromLatin1(encrypted));
#else
        return fail(QStringLiteral("当前平台不支持 Windows DPAPI，凭据未保存"));
#endif
    }
    m_dirty=true; m_timer.start(); emit changed(); return true;
}

QString SettingsStore::secret(const QString &key) const
{
    if(!m_secrets.contains(key))return {};
#ifdef Q_OS_WIN
    auto bytes=QByteArray::fromBase64(m_secrets.value(key).toString().toLatin1());
    DATA_BLOB input{static_cast<DWORD>(bytes.size()),reinterpret_cast<BYTE *>(bytes.data())},output{};
    if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)) {
        fail(QStringLiteral("Windows 凭据解密失败，请重新输入：%1").arg(GetLastError())); return {};
    }
    const auto plain=QString::fromUtf8(reinterpret_cast<const char *>(output.pbData),output.cbData);
    SecureZeroMemory(output.pbData,output.cbData); LocalFree(output.pbData); return plain;
#else
    fail(QStringLiteral("当前平台不支持 Windows DPAPI")); return {};
#endif
}
