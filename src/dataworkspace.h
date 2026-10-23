#pragma once
#include <QObject>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QTemporaryDir>
#include <memory>
#include "payloaddecoder.h"

class DataWorkspace : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantList samples READ samples NOTIFY changed)
    Q_PROPERTY(QStringList fields READ fields NOTIFY changed)
    Q_PROPERTY(QUrl mediaUrl READ mediaUrl NOTIFY changed)
    Q_PROPERTY(QString mediaKind READ mediaKind NOTIFY changed)
    Q_PROPERTY(bool capturing READ capturing NOTIFY changed)
    Q_PROPERTY(qint64 captured READ captured NOTIFY changed)
public:
    explicit DataWorkspace(QObject *parent=nullptr);
    bool enabled() const {return m_enabled;}
    void setEnabled(bool value) {m_enabled=value;emit changed();}
    QString text() const {return m_text;}
    QString error() const {return m_error;}
    QVariantList samples() const {return m_samples;}
    QStringList fields() const {return m_fields;}
    QUrl mediaUrl() const {return m_mediaUrl;}
    QString mediaKind() const {return m_mediaKind;}
    bool capturing() const {return bool(m_capture);}
    qint64 captured() const {return m_captured;}
    Q_INVOKABLE bool configure(const QString &format,const QString &schema);
    Q_INVOKABLE bool openFile(const QUrl &url,const QString &format);
    Q_INVOKABLE bool beginCapture(const QUrl &url);
    Q_INVOKABLE bool finishCapture();
    Q_INVOKABLE void cancelCapture();
    Q_INVOKABLE void clear();
    void ingest(const QByteArray &payload,bool captureEligible=true);
signals:
    void changed();
private:
    void record(const QByteArray &payload,qsizetype limit=65536);
    void schedule();
    bool fail(const QString &message);
    PayloadDecoder m_decoder;
    QTimer m_refresh;
    QString m_text=QStringLiteral("配置数据格式后应用，或载入本地文件。"),m_error,m_mediaKind;
    QUrl m_mediaUrl;
    QVariantList m_samples;
    QStringList m_fields;
    std::unique_ptr<QSaveFile> m_capture;
    std::unique_ptr<QTemporaryDir> m_captureDir;
    QString m_capturePath;
    qint64 m_captured=0,m_index=0;
    bool m_enabled=false;
};
