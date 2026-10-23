#pragma once
#include <QObject>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include "settingsstore.h"

class QuickCommands : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit QuickCommands(SettingsStore *store,QObject *parent=nullptr);
    ~QuickCommands() override;
    QString error() const { return m_error; }
    Q_INVOKABLE QVariantList entries(const QString &scope) const;
    Q_INVOKABLE QString put(const QString &scope,const QVariantMap &entry);
    Q_INVOKABLE bool remove(const QString &scope,const QString &id);
    Q_INVOKABLE bool importFile(const QString &scope,const QUrl &url);
    Q_INVOKABLE bool exportFile(const QString &scope,const QUrl &url);
signals:
    void changed();
private:
    struct Bucket { QVariantList items; QHash<QString,qsizetype> indices; };
    bool load(const QString &scope) const;
    bool fail(const QString &message) const;
    void savePending();
    void schedule(const QString &scope);
    QPointer<SettingsStore> m_store;
    mutable QString m_error;
    mutable QHash<QString,Bucket> m_buckets;
    QSet<QString> m_pending;
    QTimer m_timer;
};
