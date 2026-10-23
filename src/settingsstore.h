#pragma once
#include <QObject>
#include <QJsonObject>
#include <QTimer>
#include <QVariant>

class SettingsStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString directory READ directory NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit SettingsStore(const QString &directory=QString(), QObject *parent=nullptr);
    ~SettingsStore() override;
    QString directory() const { return m_directory; }
    QString error() const { return m_error; }
    Q_INVOKABLE QVariant value(const QString &key, const QVariant &fallback=QVariant()) const;
    Q_INVOKABLE bool setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE bool flush();
    Q_INVOKABLE bool setSecret(const QString &key, const QString &value);
    Q_INVOKABLE QString secret(const QString &key) const;
signals:
    void changed();
    void aboutToFlush();
private:
    bool fail(const QString &message) const;
    QString m_directory;
    mutable QString m_error;
    QJsonObject m_values, m_secrets;
    QTimer m_timer;
    bool m_dirty=false, m_blocked=false;
};
