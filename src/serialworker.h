#pragma once
#include <QObject>
#include <QSerialPort>
#include <QVariantMap>

class SerialWorker : public QObject {
    Q_OBJECT
public slots:
    void open(const QVariantMap &config);
    void close();
    void write(const QByteArray &bytes);
    void setLines(bool dtr, bool rts);
    void acknowledgeReceive();
signals:
    void opened(int baud);
    void closed();
    void received(const QByteArray &bytes);
    void transmitted(qint64 count);
    void fault(const QString &message);
private:
    void drain();
    QSerialPort *m_port = nullptr;
    int m_inflight = 0;
};
