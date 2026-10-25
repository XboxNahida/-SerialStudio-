#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantList>

// 只载入说明/候选数据；指令内容永不在此执行。
class CommandCatalog final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString version READ version NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString directory READ directory CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY changed)
public:
    CommandCatalog(QString appVersion,QString root=QString(),QString seed=QString(),QObject *parent=nullptr);
    QString version() const {return m_version;}
    QString source() const {return m_source;}
    QString status() const {return m_status;}
    QString directory() const {return m_root;}
    int count() const {return int(m_entries.size());}
    bool hasPrevious() const;
    QVariantList entries() const {return m_entries;}
    static constexpr qint64 maximumBytes=16*1024*1024;
    static bool validVersion(const QString &version);
    static bool validate(const QByteArray &bytes,const QString &appVersion,QString *error=nullptr);
    bool apply(const QByteArray &bytes);
    Q_INVOKABLE bool reload();
    Q_INVOKABLE bool importFile(const QUrl &url);
    Q_INVOKABLE bool exportFile(const QUrl &url);
    Q_INVOKABLE bool rollback();
    Q_INVOKABLE void openDirectory();
signals:
    void changed();
    void commandsChanged();
private:
    bool fail(const QString &message);
    void adopt(const QByteArray &bytes,const QString &source);
    QString m_appVersion,m_root,m_seed,m_version,m_source,m_status;
    QByteArray m_bytes;
    QVariantList m_entries;
};
