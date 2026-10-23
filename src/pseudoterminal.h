#pragma once
#include <QObject>
#include <QTimer>
#include <QByteArray>
#include <memory>

class PseudoTerminal : public QObject {
    Q_OBJECT
public:
    explicit PseudoTerminal(QObject *parent=nullptr);
    ~PseudoTerminal() override;
    bool start(const QString &program,const QStringList &arguments,int columns,int rows);
    // 成功表示已进入有界输入队列；实际管道错误通过 fault 异步通知。
    bool write(const QByteArray &bytes);
    void resize(int columns,int rows);
    void close();
    bool running() const;
    QString error() const {return m_error;}
signals:
    void received(const QByteArray &bytes);
    void finished(int code);
    void fault(const QString &message);
private:
    struct Native;
    std::unique_ptr<Native> m_native;
    QTimer m_poll;
    QString m_error;
};
