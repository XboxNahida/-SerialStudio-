#include "session.h"
#include "serialworker.h"
#include "networkworker.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSerialPortInfo>
#include <limits>

namespace {
QString stamp() { return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")); }
}
Session::Session(QObject *parent, bool persist) : QObject(parent), m_worker(new SerialWorker), m_network(new NetworkWorker), m_persist(persist)
{
    m_worker->moveToThread(&m_thread);
    m_network->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_network, &QObject::deleteLater);
    connect(this, &Session::requestNetworkOpen, m_network, &NetworkWorker::open);
    connect(this, &Session::requestNetworkClose, m_network, &NetworkWorker::close);
    connect(this, &Session::requestNetworkWrite, m_network, &NetworkWorker::write);
    connect(this, &Session::networkConsumed, m_network, &NetworkWorker::acknowledgeReceive);
    connect(m_network, &NetworkWorker::opened, this, [this](const QString &description, bool canWrite) {
        m_busy = false; m_connected = true; m_canWrite = canWrite; m_status = description; m_lastError.clear();
        emit stateChanged(); emit notify(description);
    });
    connect(m_network, &NetworkWorker::availability, this, [this](bool canWrite, const QString &description) {
        if(!canWrite && m_analysis.capturing()) {m_analysis.cancelCapture();report(QStringLiteral("TCP 对端断开，已取消尚未提交的数据收集"));}
        stopPeriodic(); m_canWrite = canWrite; m_status = description;
        m_parser.reset(); m_idle.stop(); emit stateChanged();
    });
    connect(m_network, &NetworkWorker::closed, this, &Session::disconnected);
    connect(m_network, &NetworkWorker::received, this, [this](const QByteArray &b, bool fromTarget) { receive(b, false, fromTarget); emit networkConsumed(); });
    connect(m_network, &NetworkWorker::transmitted, this, [this](qint64 n) { m_tx += qulonglong(n); });
    connect(m_network, &NetworkWorker::fault, this, [this](const QString &s) { stopPeriodic(); report(s); });
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &Session::requestOpen, m_worker, &SerialWorker::open);
    connect(this, &Session::requestClose, m_worker, &SerialWorker::close);
    connect(this, &Session::requestWrite, m_worker, &SerialWorker::write);
    connect(this, &Session::requestLines, m_worker, &SerialWorker::setLines);
    connect(this, &Session::receivedConsumed, m_worker, &SerialWorker::acknowledgeReceive);
    connect(m_worker, &SerialWorker::opened, this, [this](int baud) {
        m_busy = false; m_connected = true; m_lastError.clear();
        m_status = QStringLiteral("已连接 · %1 bps").arg(baud);
        if (m_persist) m_settings.setValue("connection", m_config);
        emit stateChanged(); emit notify(QStringLiteral("串口连接成功"));
    });
    connect(m_worker, &SerialWorker::closed, this, &Session::disconnected);
    connect(m_worker, &SerialWorker::received, this, [this](const QByteArray &b) { receive(b); emit receivedConsumed(); });
    connect(m_worker, &SerialWorker::transmitted, this, [this](qint64 n) { m_tx += qulonglong(n); });
    connect(m_worker, &SerialWorker::fault, this, [this](const QString &s) { stopPeriodic(); report(s); });
    connect(&m_uiTimer, &QTimer::timeout, this, &Session::flushUi);
    m_uiTimer.start(100);
    m_idle.setSingleShot(true);
    m_idle.setTimerType(Qt::PreciseTimer);
    connect(&m_idle, &QTimer::timeout, this, [this] { collect(m_parser.flush(), m_demoPending); });
    connect(&m_repeat, &QTimer::timeout, this, [this] {
        if (!writable()) { stopPeriodic(); return; }
        writeBytes(m_repeatBytes);
        enqueue(m_pendingTerminal, {stamp(), "TX", QStringLiteral("定时发送 · 已排队"), {}, m_repeatBytes, true});
    });
    connect(&m_rateTimer, &QTimer::timeout, this, [this] {
        m_rate = double(m_rx-m_previousRx); m_previousRx = m_rx; emit statsChanged();
    });
    m_rateTimer.start(1000);
    m_parser.configure(m_mode);
    m_thread.start(); refreshPorts();
}
Session::~Session()
{
    disconnect(m_worker, nullptr, this, nullptr);
    disconnect(m_network, nullptr, this, nullptr);
    QMetaObject::invokeMethod(m_worker, &SerialWorker::close, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_network, &NetworkWorker::close, Qt::BlockingQueuedConnection);
    m_thread.quit(); m_thread.wait();
}
QVariantMap Session::preferences() const
{
    return m_persist ? QVariantMap{{"dark", m_settings.value("dark", true)},
                                   {"connection", m_settings.value("connection", QVariantMap{})}} : QVariantMap{};
}
void Session::refreshPorts()
{
    m_ports.clear();
    for (const auto &port : QSerialPortInfo::availablePorts()) {
        m_ports.append(QVariantMap{{"name", port.portName()}, {"label", port.portName() + " · " + port.description()},
                                  {"description", port.description()}, {"manufacturer", port.manufacturer()}});
    }
    emit portsChanged();
}
void Session::report(const QString &message)
{
    m_lastError = message; ++m_errorCount;
    enqueue(m_pendingTerminal, {stamp(), "SYS", QStringLiteral("提示"), message, {}, false});
    emit stateChanged(); emit statsChanged(); emit notify(message);
}
bool Session::openPort(const QVariantMap &c)
{
    if (m_busy || m_connected) { report(QStringLiteral("请先断开当前连接")); return false; }
    bool converted = false;
    const auto baud = c.value("baud").toLongLong(&converted);
    if (!converted || baud < 1 || baud > std::numeric_limits<qint32>::max()
        || c.value("port").toString().trimmed().isEmpty()
        || !QList<int>{5,6,7,8}.contains(c.value("dataBits").toInt())
        || !QList<int>{0,2,3,4,5}.contains(c.value("parity").toInt())
        || !QList<int>{1,2,3}.contains(c.value("stopBits").toInt())
        || !QList<int>{0,1,2}.contains(c.value("flow").toInt())) {
        report(QStringLiteral("连接参数无效，请检查端口、波特率、数据位和校验设置")); return false;
    }
    m_parser.reset(); m_idle.stop(); m_demoPending = false; m_transport = 0; m_canWrite = true;
    m_config = c; m_config["baud"] = int(baud);
    m_portName = c.value("port").toString(); m_busy = true;
    m_status = QStringLiteral("正在连接…"); m_lastError.clear();
    emit stateChanged(); emit requestOpen(m_config); return true;
}
void Session::closePort()
{
    stopPeriodic();
    if (m_connected || m_busy) {
        m_busy = true; emit stateChanged();
        if (m_transport == 0) emit requestClose(); else emit requestNetworkClose();
    }
}
bool Session::openNetwork(const QVariantMap &config)
{
    if (m_busy || m_connected) { report(QStringLiteral("请先关闭当前连接")); return false; }
    bool ok = false;
    const int mode = config.value("mode").toInt(&ok);
    if (!ok || mode < 0 || mode > 2) { report(QStringLiteral("网络模式无效")); return false; }
    m_transport = mode+1; m_canWrite = false; m_busy = true; m_lastError.clear();
    m_portName = config.value("host").toString()+":"+config.value("port").toString();
    m_status = QStringLiteral("正在打开网络…"); m_config = config;
    m_parser.reset(); m_idle.stop(); m_demoPending = false;
    emit stateChanged(); emit requestNetworkOpen(config); return true;
}
void Session::disconnected()
{
    if(m_analysis.capturing())m_analysis.cancelCapture();
    m_connected = false; m_busy = false; m_canWrite = false; stopPeriodic(); m_idle.stop();
    collect(m_parser.flush(), false); m_parser.reset();
    m_status = QStringLiteral("未连接"); emit stateChanged();
}
void Session::writeBytes(const QByteArray &bytes)
{ if (m_transport == 0) emit requestWrite(bytes); else emit requestNetworkWrite(bytes); }
bool Session::sendBytes(const QByteArray &bytes, const QString &title)
{
    if (!writable() || bytes.isEmpty() || bytes.size() > (m_transport == 3 ? 65507 : 65536)) {
        report(QStringLiteral("当前来源不可发送或报文超过单次容量")); return false;
    }
    writeBytes(bytes); enqueue(m_pendingTerminal, {stamp(), "TX", title, {}, bytes, true}); return true;
}
bool Session::makePayload(const QString &text, bool hex, int newline, QByteArray *bytes)
{
    if (text.size() > 131072) { report(QStringLiteral("单次发送内容过大（上限 128K 字符）")); return false; }
    if (hex) {
        QString error;
        if (!decodeHex(text, bytes, &error)) { report(error); return false; }
    } else *bytes = text.toUtf8();
    if (newline == 1) bytes->append('\n');
    else if (newline == 2) bytes->append("\r\n");
    else if (newline == 3) bytes->append('\r');
    if (bytes->isEmpty() || bytes->size() > 65536) {
        report(QStringLiteral("发送内容不能为空，且每次最多 65536 字节")); return false;
    }
    return true;
}
bool Session::send(const QString &text, bool hex, int newline)
{
    if (!writable()) { report(QStringLiteral("请先连接可发送的数据来源")); return false; }
    QByteArray bytes;
    if (!makePayload(text, hex, newline, &bytes)) return false;
    return sendBytes(bytes, QStringLiteral("发送 · 已加入队列"));
}
bool Session::startPeriodic(const QString &text, bool hex, int newline, int interval)
{
    if (!writable()) { report(QStringLiteral("请先连接可发送的数据来源")); return false; }
    if (interval < 10 || interval > 3600000) { report(QStringLiteral("发送间隔范围为 10–3600000 ms")); return false; }
    if (!makePayload(text, hex, newline, &m_repeatBytes)) return false;
    if (m_transport == 3 && m_repeatBytes.size() > 65507) { report(QStringLiteral("UDP 单包最多 65507 字节")); return false; }
    m_repeat.start(interval); emit stateChanged(); return true;
}
void Session::stopPeriodic() { m_repeat.stop(); m_repeatBytes.clear(); emit stateChanged(); }
bool Session::configureParser(int mode, const QString &head, const QString &tail)
{
    if(m_analysis.capturing()){report(QStringLiteral("请先完成或取消数据收集，再更改分帧配置"));return false;}
    if (mode < 0 || mode > 3) { report(QStringLiteral("未知解析模式")); return false; }
    QByteArray h, t; QString error;
    if (mode == 3 && (!decodeHex(head, &h, &error) || !decodeHex(tail, &t, &error)
                      || h.size() > 32 || t.size() > 32)) {
        report(error.isEmpty() ? QStringLiteral("帧头与帧尾各最多 32 字节") : error); return false;
    }
    m_idle.stop(); m_mode = ProtocolParser::Mode(mode); m_demoPending = false;
    if (mode == 3) { m_customHead = h; m_customTail = t; }
    m_parser.configure(m_mode, h, t); emit stateChanged(); return true;
}
void Session::enqueue(QList<EventEntry> &queue, EventEntry event)
{
    const bool truncated = event.bytes.size() > 4096 || event.detail.size() > 8192;
    event.bytes.truncate(4096); event.detail.truncate(8192);
    if (truncated) {
        const auto notice = QStringLiteral("\n显示已截断：字节最多 4096，详情最多 8192 字符；完整数据仍参与解析。");
        event.title += QStringLiteral("（显示已截断）");
        event.detail.truncate(8192-notice.size());
        event.detail += notice;
    }
    if (queue.size() >= 1000) { ++m_pendingDropped; return; }
    queue.append(std::move(event));
}
void Session::receive(const QByteArray &bytes, bool demoSource, bool bridgeEligible)
{
    if (!demoSource) m_rx += qulonglong(bytes.size());
    m_demoPending = demoSource;
    m_bridgeEligible = bridgeEligible;
    enqueue(m_pendingTerminal, {stamp(), demoSource ? "DEMO" : "RX",
                               demoSource ? QStringLiteral("离线演示 · 非串口输入") : QStringLiteral("接收 · %1 字节").arg(bytes.size()),
                               bytes.size()>4096 ? QStringLiteral("显示前 4096 字节；协议解析仍处理完整数据") : QString{}, bytes, true});
    if (m_transport == 3) m_parser.reset(); // UDP 各包独立，禁止把两个数据报拼成桥接帧。
    collect(m_parser.feed(bytes), demoSource);
    if (m_transport == 3) collect(m_parser.flush(), demoSource);
    if (m_mode == ProtocolParser::Modbus) {
        // 以字符时间估算并设置 5ms 下限；Windows 定时不能代替精确总线时序。
        const int baud = qMax(1, m_config.value("baud", 115200).toInt());
        m_idle.start(qMax(5, int(38500.0 / baud + 1)));
    }
}
void Session::collect(const QList<ParsedFrame> &frames, bool demoSource)
{
    for (const auto &f : frames) {
        ++m_frameCount; if (!f.valid) ++m_errorCount;
        if (f.valid && !demoSource && m_bridgeEligible) emit frameReceived(f.bytes);
        if(f.valid) {
            auto payload=f.bytes;
            if(m_mode==ProtocolParser::Custom) payload=payload.mid(m_customHead.size(),payload.size()-m_customHead.size()-m_customTail.size());
            m_analysis.ingest(payload,!demoSource);
        }
        enqueue(m_pendingFrames, {stamp(), demoSource ? "DEMO" : "PARSE", f.title, f.detail, f.bytes, f.valid});
    }
}
void Session::flushUi()
{
    if (m_pendingDropped) {
        const auto count = m_pendingDropped; m_pendingDropped = 0;
        m_terminal.append({{stamp(), "SYS", QStringLiteral("界面节流"),
                            QStringLiteral("本批次省略 %1 条显示记录；接收字节计数与解析仍继续").arg(count), {}, false}});
    }
    m_terminal.append(std::move(m_pendingTerminal)); m_pendingTerminal.clear();
    m_frames.append(std::move(m_pendingFrames)); m_pendingFrames.clear(); emit statsChanged();
}
void Session::clearTerminal() { m_pendingTerminal.clear(); m_terminal.clear(); }
void Session::clearFrames() { m_pendingFrames.clear(); m_frames.clear(); }
void Session::resetStats() { m_rx = m_tx = m_previousRx = 0; m_rate = 0; m_frameCount = m_errorCount = 0; emit statsChanged(); }
void Session::demo()
{
    if (m_connected || m_busy) { report(QStringLiteral("请断开串口后使用离线演示")); return; }
    m_parser.reset();
    QByteArray sample;
    if (m_mode == ProtocolParser::Modbus) sample = QByteArray::fromHex("01030000000ac5cd010302000a3843");
    else if (m_mode == ProtocolParser::Custom) sample = m_customHead + QByteArray::fromHex("010219002a") + m_customTail;
    else sample = QStringLiteral("设备启动完成 · 离线演示\r\n{\"temperature\":26.5,\"voltage\":3.3,\"status\":\"OK\"}\r\n").toUtf8();
    receive(sample, true); emit notify(QStringLiteral("已加载离线演示，未访问物理串口"));
}
bool Session::saveLog(const QUrl &url)
{
    if (!url.isLocalFile()) { report(QStringLiteral("请选择本地文件")); return false; }
    flushUi(); QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly)) { report(file.errorString()); return false; }
    for (const auto &e : m_terminal.entries()) {
        const QJsonObject obj{{"time",e.stamp},{"source",e.kind},{"title",e.title},{"detail",e.detail},
                              {"hex",QString::fromLatin1(e.bytes.toHex())},{"valid",e.valid}};
        const auto line = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
        if (file.write(line) != line.size()) { report(file.errorString()); return false; }
    }
    if (!file.commit()) { report(file.errorString()); return false; }
    emit notify(QStringLiteral("已保存当前保留的终端记录（JSONL / UTF-8）")); return true;
}
void Session::savePreference(const QString &key, const QVariant &value)
{ if (m_persist && key == QStringLiteral("dark")) m_settings.setValue(key, value); }
void Session::setLines(bool dtr, bool rts) { if (m_connected && m_transport == 0) emit requestLines(dtr, rts); }
