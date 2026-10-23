#include "serialworker.h"

void SerialWorker::open(const QVariantMap &c)
{
    close();
    if (!m_port) {
        m_port = new QSerialPort(this);
        m_port->setReadBufferSize(262144);
        connect(m_port, &QSerialPort::readyRead, this, &SerialWorker::drain);
        connect(m_port, &QSerialPort::bytesWritten, this, &SerialWorker::transmitted);
        connect(m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
            if (error == QSerialPort::NoError || !m_port->isOpen()) return;
            emit fault(m_port->errorString());
            if (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError
                || error == QSerialPort::ReadError || error == QSerialPort::WriteError) close();
        });
    }
    m_port->setPortName(c.value("port").toString());
    bool ok = m_port->setBaudRate(c.value("baud").toInt());
    ok = m_port->setDataBits(QSerialPort::DataBits(c.value("dataBits").toInt())) && ok;
    ok = m_port->setParity(QSerialPort::Parity(c.value("parity").toInt())) && ok;
    ok = m_port->setStopBits(QSerialPort::StopBits(c.value("stopBits").toInt())) && ok;
    ok = m_port->setFlowControl(QSerialPort::FlowControl(c.value("flow").toInt())) && ok;
    if (!ok || !m_port->open(QIODevice::ReadWrite)) {
        emit fault(QStringLiteral("打开串口失败：") + m_port->errorString());
        emit closed(); return;
    }
    emit opened(m_port->baudRate());
}
void SerialWorker::close()
{ if (m_port && m_port->isOpen()) { m_port->close(); emit closed(); } }
void SerialWorker::drain()
{
    if (!m_port || !m_port->isOpen()) return;
    // 最多 8 个跨线程接收块，避免 UI 繁忙时无限积压 Qt queued 信号。
    while (m_inflight < 8 && m_port->bytesAvailable() > 0) {
        const auto bytes = m_port->read(32768);
        if (bytes.isEmpty()) break;
        ++m_inflight; emit received(bytes);
    }
    if (m_inflight >= 8 && m_port->bytesAvailable() >= 262144) {
        emit fault(QStringLiteral("接收处理积压达到容量上限，已断开串口；本次采集可能不完整"));
        close();
    }
}
void SerialWorker::acknowledgeReceive() { if (m_inflight > 0) --m_inflight; drain(); }
void SerialWorker::write(const QByteArray &bytes)
{
    if (!m_port || !m_port->isOpen()) { emit fault(QStringLiteral("串口尚未连接")); return; }
    if (m_port->bytesToWrite() > 262144) { emit fault(QStringLiteral("发送队列繁忙，请降低发送频率")); return; }
    if (m_port->write(bytes) != bytes.size()) emit fault(QStringLiteral("数据未能完整加入发送队列"));
}
void SerialWorker::setLines(bool dtr, bool rts)
{
    if (!m_port || !m_port->isOpen()) return;
    if (!m_port->setDataTerminalReady(dtr)) emit fault(QStringLiteral("驱动不支持 DTR 设置"));
    if (m_port->flowControl() != QSerialPort::HardwareControl && !m_port->setRequestToSend(rts))
        emit fault(QStringLiteral("驱动不支持 RTS 设置"));
}
