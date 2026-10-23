#pragma once
#include "adbdiscovery.h"
#include "filetransfer.h"
#include <QThread>

// 每个作业冻结目标，目录与传输在后台进行；不自动连接或递归读取文件。
class RemoteFileWorker : public QObject {
    Q_OBJECT
public:
    std::atomic_bool cancelled{false};
public slots:
    void execute(const QVariantMap &job);
signals:
    void completed(const QVariantMap &result);
    void progress(int percent);
};

class RemoteFiles : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool success READ success NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(AdbDiscovery* adb READ adb CONSTANT)
    Q_PROPERTY(QString clipboardName READ clipboardName NOTIFY changed)
    Q_PROPERTY(bool clipboardCut READ clipboardCut NOTIFY changed)
public:
    explicit RemoteFiles(QObject *parent=nullptr);
    ~RemoteFiles() override;
    bool busy() const {return m_busy;}
    bool success() const {return m_success;}
    QString status() const {return m_status;}
    QString path() const {return m_path;}
    QString kind() const {return m_kind;}
    QVariantList entries() const {return m_entries;}
    int progress() const {return m_progress;}
    AdbDiscovery *adb() {return &m_adb;}
    QString clipboardName() const {return m_clipboard.value("entry").toMap().value("name").toString();}
    bool clipboardCut() const {return m_clipboard.value("cut").toBool();}
    Q_INVOKABLE bool browse(const QString &kind,const QString &path,const QVariantMap &options={});
    Q_INVOKABLE bool upload(const QString &localFile,bool allowOverwrite=false);
    Q_INVOKABLE bool download(const QString &entryId,const QString &localDirectory);
    Q_INVOKABLE bool copyEntry(const QString &entryId,bool cut=false);
    Q_INVOKABLE bool paste();
    Q_INVOKABLE bool removeEntry(const QString &entryId,bool confirmed=false);
    Q_INVOKABLE void clearClipboard();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void cancel();
    static QVariantList parseAdbListing(const QByteArray &data,const QString &path,QString *error);
    static QVariantList parseSftpListing(const QByteArray &data,const QString &path,QString *error);
    static QString shellQuote(const QString &value);
    static QByteArray sftpListBatch(const QString &path);
    static QByteArray sftpRemoveBatch(const QString &path,bool directory);
    static bool validRemotePath(const QString &path);
signals:
    void changed();
    void requested(const QVariantMap &job);
private:
    bool fail(const QString &message);
    bool startJob(QVariantMap job);
    bool startTransfer(bool upload,const QString &local,const QString &remote,bool overwrite=false);
    AdbDiscovery m_adb;
    FileTransfer m_transfer;
    QThread m_thread;
    RemoteFileWorker *m_worker;
    QVariantList m_entries;
    QVariantMap m_options;
    QVariantMap m_clipboard;
    QString m_path,m_kind,m_status=QStringLiteral("选择 USB 或网络方式，点击打开目录。ADB/MTP 无需 Windows 盘符。");
    bool m_busy=false,m_success=true,m_listed=false;
    int m_progress=0;
};
