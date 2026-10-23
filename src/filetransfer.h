#pragma once
#include <QObject>
#include <QThread>
#include <QProcess>
#include <QTimer>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantMap>
#include <atomic>
#include <memory>

class CopyWorker : public QObject {
    Q_OBJECT
public:
    std::atomic_bool cancelled{false};
public slots:
    void copy(const QString &source,const QString &directory);
signals:
    void progress(int percent);
    void done(bool ok,const QString &message,const QUrl &file);
};

class FileTransfer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool success READ success NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(QUrl resultFile READ resultFile NOTIFY changed)
    Q_PROPERTY(QVariantList volumes READ volumes NOTIFY changed)
public:
    explicit FileTransfer(QObject *parent=nullptr);
    ~FileTransfer() override;
    bool busy() const {return m_busy;}
    bool success() const {return m_success;}
    QString status() const {return m_status;}
    int progress() const {return m_progress;}
    QUrl resultFile() const {return m_result;}
    QVariantList volumes() const {return m_volumes;}
    Q_INVOKABLE void refreshVolumes();
    Q_INVOKABLE QUrl localUrl(const QString &path) const {return QUrl::fromLocalFile(path);}
    Q_INVOKABLE QString localPath(const QUrl &url) const {return url.toLocalFile();}
    Q_INVOKABLE bool start(int mode,const QUrl &source,const QUrl &destination,const QVariantMap &options);
    Q_INVOKABLE void cancel();
    static bool sftpCommands(int mode,const QString &local,const QString &temporary,const QVariantMap &options,
                             QStringList *arguments,QByteArray *batch,QString *error);
signals:
    void changed();
    void copyRequested(const QString &source,const QString &directory);
private:
    bool finish(bool success,const QString &message,const QUrl &result=QUrl());
    void processFinished(int code,QProcess::ExitStatus state);
    QThread m_thread;
    CopyWorker *m_copy;
    QProcess m_process;
    QTimer m_timeout;
    std::unique_ptr<QTemporaryDir> m_temp;
    QVariantList m_volumes;
    QByteArray m_log;
    QString m_status=QStringLiteral("选择 USB 磁盘复制或 SFTP 网络传输。"),m_final,m_partial,m_abort;
    QUrl m_result;
    int m_progress=0,m_mode=0;
    bool m_busy=false,m_success=true;
};
