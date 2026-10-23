#include "dataworkspace.h"
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <cmath>

DataWorkspace::DataWorkspace(QObject *parent):QObject(parent)
{m_refresh.setSingleShot(true);m_refresh.setInterval(100);connect(&m_refresh,&QTimer::timeout,this,&DataWorkspace::changed);}
void DataWorkspace::schedule() {if(!m_refresh.isActive())m_refresh.start();}
bool DataWorkspace::fail(const QString &message) {m_error=message;emit changed();return false;}
bool DataWorkspace::configure(const QString &format,const QString &schema)
{QString error;if(!m_decoder.configure(format,schema,&error))return fail(error);clear();m_enabled=true;emit changed();return true;}
void DataWorkspace::clear()
{m_samples.clear();m_fields.clear();m_index=0;m_text.clear();m_error.clear();m_mediaUrl=QUrl();m_mediaKind.clear();emit changed();}
void DataWorkspace::record(const QByteArray &payload,qsizetype limit)
{
    const auto result=m_decoder.decode(payload,limit);m_error=result.error;
    if(!result.error.isEmpty()) {schedule();return;}
    m_text=result.text.left(32768);if(result.text.size()>32768)m_text+=QStringLiteral("\n显示已截断至 32K 字符");
    QVariantMap values;
    for(auto it=result.values.cbegin();it!=result.values.cend();++it) {
        if(values.size()>=64)break;
        if(it.value().metaType().id()!=QMetaType::Double && it.value().metaType().id()!=QMetaType::LongLong && it.value().metaType().id()!=QMetaType::Int)continue;
        const double n=it.value().toDouble();if(std::isfinite(n))values[it.key()]=n;
    }
    if(!values.isEmpty()) {
        m_fields=values.keys();m_samples.append(QVariantMap{{"sample",++m_index},{"values",values}});
        if(m_samples.size()>256)m_samples.removeFirst();
    }
    schedule();
}
void DataWorkspace::ingest(const QByteArray &payload,bool captureEligible)
{
    if(m_capture && captureEligible) {
        if(payload.size()>268435456-m_captured || m_capture->write(payload)!=payload.size()) {
            cancelCapture();fail(QStringLiteral("收集失败或超过 256 MiB，临时内容已取消"));return;
        }
        m_captured+=payload.size();schedule();
    }
    if(m_enabled)record(payload);
}
bool DataWorkspace::beginCapture(const QUrl &url)
{
    if(capturing())return fail(QStringLiteral("请先结束或取消当前收集"));
    if(!url.isLocalFile() || QFileInfo::exists(url.toLocalFile()))return fail(QStringLiteral("选择尚不存在的本地输出文件，不覆盖已有文件"));
    auto directory=std::make_unique<QTemporaryDir>(QFileInfo(url.toLocalFile()).absolutePath()+"/.serialstudio-capture-XXXXXX");
    if(!directory->isValid())return fail(QStringLiteral("无法创建收集临时目录"));
    auto file=std::make_unique<QSaveFile>(directory->filePath("data.part"));file->setDirectWriteFallback(false);
    if(!file->open(QIODevice::WriteOnly))return fail(file->errorString());
    m_capturePath=url.toLocalFile();m_capture=std::move(file);m_captureDir=std::move(directory);m_captured=0;m_error.clear();emit changed();return true;
}
bool DataWorkspace::finishCapture()
{
    if(!m_capture)return fail(QStringLiteral("当前没有收集任务"));
    if(QFileInfo::exists(m_capturePath)) {cancelCapture();return fail(QStringLiteral("目标已存在，取消收集以免覆盖"));}
    const bool ok=m_capture->commit();const auto error=m_capture->errorString();m_capture.reset();
    const bool renamed=ok && QFile::rename(m_captureDir->filePath("data.part"),m_capturePath);m_captureDir.reset();emit changed();
    if(!ok || !renamed)return fail(ok?QStringLiteral("目标已存在或无法提交，未覆盖已有文件"):error);
    m_text=QStringLiteral("已保存 %1 字节：%2。可点击载入文件预览。").arg(m_captured).arg(m_capturePath);emit changed();return true;
}
void DataWorkspace::cancelCapture() {m_capture.reset();m_captureDir.reset();emit changed();}
bool DataWorkspace::openFile(const QUrl &url,const QString &selected)
{
    if(!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isFile())return fail(QStringLiteral("请选择存在的本地文件"));
    const auto path=url.toLocalFile();QString format=selected;
    if(format=="auto") {
        const auto mime=QMimeDatabase().mimeTypeForFile(path).name();const auto ext=QFileInfo(path).suffix().toLower();
        if(mime.startsWith("image/"))format="image";
        else if(mime.startsWith("audio/"))format="audio";
        else if(mime.startsWith("video/") || QStringList{"mp4","mkv","avi"}.contains(ext))format="video";
        else if(ext=="json")format="json";else if(ext=="csv")format="csv";
        else if(ext=="bin")format="hex";else format="utf8";
    }
    if(QStringList{"image","video","audio"}.contains(format)) {
        if(format=="image") {
            QImageReader reader(path);const auto size=reader.size();
            if(!reader.canRead() || !size.isValid() || qint64(size.width())*size.height()>16777216)
                return fail(QStringLiteral("图片格式无效或超过 1600 万像素预览上限"));
        }
        clear();m_enabled=false;m_mediaUrl=url;m_mediaKind=format;
        m_text=QStringLiteral("%1\n%2 字节；媒体仅在点击播放后解码，具体支持取决于编码。").arg(QFileInfo(path).fileName()).arg(QFileInfo(path).size());
        emit changed();return true;
    }
    if(!QStringList{"utf8","json","csv","binary","hex"}.contains(format))return fail(QStringLiteral("未知文件格式"));
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return fail(file.errorString());
    if(file.size()>4194304)return fail(QStringLiteral("文本/数据文件载入上限 4 MiB；大文件请使用文件传输，媒体用相应预览选项"));
    const auto data=file.readAll();if(file.error()!=QFile::NoError)return fail(file.errorString());
    if(format!="binary") {QString error;if(!m_decoder.configure(format,"[]",&error))return fail(error);}
    clear();m_enabled=false;
    if(format=="csv") {
        const auto rows=data.split('\n');if(rows.size()>10000)return fail(QStringLiteral("CSV 载入最多 10000 行"));
        for(auto row:rows) {if(row.trimmed().isEmpty())continue;record(row);if(!m_error.isEmpty())return false;}
    } else if(format=="json")record(data,4194304);
    else {record(data.left(65536));if(data.size()>65536)m_text+=QStringLiteral("\n仅解释文件前 64 KiB");}
    emit changed();return m_error.isEmpty();
}
