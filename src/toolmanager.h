#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include <QTimer>
#include <memory>
class SettingsStore;
class SystemProxy;
class QNetworkReply;

class ToolManager final: public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList components READ components NOTIFY changed)
    Q_PROPERTY(QString directory READ directory CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
public:
    ToolManager(SettingsStore *settings,SystemProxy *proxy,const QString &root=QString(),QNetworkAccessManager *network=nullptr,QObject *parent=nullptr);
    ~ToolManager() override;
    QVariantList components() const;
    QString directory() const;
    QString status() const {return m_status;}
    bool busy() const {return bool(m_staging);}
    double progress() const {return m_progress;}
    Q_INVOKABLE bool setPath(const QString &name,const QString &path);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool install(const QString &id);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openOfficial(const QString &id);
    static bool removeManaged(const QString &root,QString *report=nullptr);
    static bool seedManaged(const QString &source,const QString &root);
signals:
    void changed();
private:
    friend class ToolManagerTests;
    void finish(const QString &message);
    void unpack();
    SettingsStore *m_settings;
    QString m_root,m_status=QStringLiteral("必备工具随安装包提供；扩展工具仅在点击后下载");
    QVariantMap m_overrides,m_active;
    QVariantList m_catalog;
    QNetworkAccessManager m_network;
    QNetworkAccessManager *m_access;
    QPointer<QNetworkReply> m_reply;
    QProcess m_extract;
    QTimer m_deadline;
    std::unique_ptr<QTemporaryDir> m_staging;
    QFile m_archive;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    qint64 m_received=0;
    double m_progress=0;
    bool m_cancelled=false,m_exceeded=false;
};
