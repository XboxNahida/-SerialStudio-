#include "payloaddecoder.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <cstring>

namespace {
int width(const QString &type)
{
    if (type == "u8" || type == "i8") return 1;
    if (type == "u16" || type == "i16") return 2;
    if (type == "u32" || type == "i32" || type == "f32") return 4;
    if (type == "f64") return 8;
    return 0;
}
bool whole(const QJsonValue &value, int low, int high)
{ const auto n=value.toDouble(-1); return value.isDouble() && n>=low && n<=high && std::floor(n)==n; }
}
bool PayloadDecoder::configure(const QString &format, const QString &schema, QString *error)
{
    error->clear();
    if (!QStringList{"utf8","json","csv","binary","hex"}.contains(format)) {
        *error=QStringLiteral("未知数据格式"); return false;
    }
    QJsonArray fields;
    if (format == "binary") {
        QJsonParseError e; const auto doc=QJsonDocument::fromJson(schema.toUtf8(), &e);
        if (schema.size()>32768 || e.error!=QJsonParseError::NoError || !doc.isArray() || doc.array().isEmpty() || doc.array().size()>64) {
            *error=QStringLiteral("字段规则需为 1–64 项的 JSON 数组（最多 32K 字符）"); return false;
        }
        fields=doc.array(); QSet<QString> names;
        for (const auto &item : fields) {
            const auto f=item.toObject(); const auto name=f.value("name").toString(), type=f.value("type").toString();
            const int bytes=width(type);
            const bool variable=type=="utf8" || type=="hex";
            if (name.trimmed().isEmpty() || name.size()>64 || names.contains(name) || !whole(f.value("offset"),0,65535)
                || (!bytes && !variable) || (variable && !whole(f.value("length"),1,4096))
                || !QStringList{"le","be"}.contains(f.value("endian").toString("le"))) {
                *error=QStringLiteral("检查字段名称唯一性、offset、type、endian 和 length"); return false;
            }
            for (auto key : {"scale","bias"}) {
                if (f.contains(key) && (!f.value(key).isDouble() || !std::isfinite(f.value(key).toDouble()))) {
                    *error=QStringLiteral("scale 和 bias 必须为有限数值"); return false;
                }
            }
            names.insert(name);
        }
    }
    m_format=format; m_fields=fields; return true;
}
DataResult PayloadDecoder::decode(const QByteArray &payload,qsizetype limit) const
{
    DataResult result;
    if (payload.size()>limit) { result.error=QStringLiteral("数据超过本次解释容量"); return result; }
    if (m_format=="utf8") { result.text=QString::fromUtf8(payload); return result; }
    if (m_format=="hex") { result.text=QString::fromLatin1(payload.toHex(' ')); return result; }
    if (m_format=="json") {
        QJsonParseError e; const auto doc=QJsonDocument::fromJson(payload,&e);
        if (e.error!=QJsonParseError::NoError || doc.isNull()) { result.error=QStringLiteral("JSON 解析失败：")+e.errorString(); return result; }
        if (doc.isObject()) result.values=doc.object().toVariantMap();
        else { int i=0; for (const auto &value: doc.array()) result.values[QStringLiteral("列%1").arg(++i)]=value.toVariant(); }
        result.text=QString::fromUtf8(doc.toJson(QJsonDocument::Indented)); return result;
    }
    if (m_format=="csv") {
        const auto text=QString::fromUtf8(payload).trimmed(); QString cell; QStringList cells;
        bool quoted=false, closed=false;
        for (qsizetype i=0;i<text.size();++i) {
            const auto ch=text[i];
            if (quoted) {
                if (ch=='"') { if (i+1<text.size() && text[i+1]=='"') {cell+='"';++i;} else {quoted=false;closed=true;} }
                else cell+=ch;
            } else if (ch==',') {cells.append(cell);cell.clear();closed=false;}
            else if (ch=='"' && cell.isEmpty() && !closed) quoted=true;
            else if (closed || ch=='"' || ch=='\n' || ch=='\r') {result.error=QStringLiteral("CSV 每帧需一个完整记录，检查引号与换行");return result;}
            else cell+=ch;
            if (cells.size()>64) {result.error=QStringLiteral("CSV 最多 64 列");return result;}
        }
        if (quoted || cells.size()>=64) {result.error=QStringLiteral("CSV 引号未闭合或列数超过 64");return result;}
        cells.append(cell);
        for (int i=0;i<cells.size();++i) { bool ok=false;const double n=cells[i].toDouble(&ok);
            result.values[QStringLiteral("列%1").arg(i+1)]=ok && std::isfinite(n) ? QVariant(n):QVariant(cells[i]); }
    } else {
        for (const auto &item:m_fields) {
            const auto f=item.toObject(); const auto name=f.value("name").toString(), type=f.value("type").toString();
            const int offset=f.value("offset").toInt(), size=width(type) ? width(type):f.value("length").toInt();
            if (offset>payload.size() || size>payload.size()-offset) {result.error=QStringLiteral("字段 %1 越过载荷边界").arg(name);result.values.clear();return result;}
            const auto data=payload.mid(offset,size);
            if (type=="utf8" || type=="hex") {result.values[name]=type=="utf8" ? QString::fromUtf8(data):QString::fromLatin1(data.toHex(' '));continue;}
            quint64 bits=0;
            for(int i=0;i<size;++i) {const auto index=f.value("endian").toString("le")=="be" ? i:size-1-i;bits=(bits<<8)|quint8(data[index]);}
            double number=0;
            if(type=="f32") {const quint32 raw=quint32(bits);float value=0;std::memcpy(&value,&raw,sizeof(value));number=value;}
            else if(type=="f64") std::memcpy(&number,&bits,sizeof(number));
            else if(type.startsWith('i') && (bits & (quint64(1)<<(size*8-1)))) number=double(bits)-std::ldexp(1.0,size*8);
            else number=double(bits);
            number=number*f.value("scale").toDouble(1)+f.value("bias").toDouble(0);
            if(!std::isfinite(number)) {result.error=QStringLiteral("字段 %1 为非有限数值").arg(name);result.values.clear();return result;}
            result.values[name]=number;
        }
    }
    result.text=QString::fromUtf8(QJsonDocument::fromVariant(result.values).toJson(QJsonDocument::Indented)); return result;
}
