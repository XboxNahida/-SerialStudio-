#include "boardcontroller.h"
#include "bridgecodec.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QString sshProgram()
{
#ifdef Q_OS_WIN
    const auto path = QDir(qEnvironmentVariable("SystemRoot", "C:/Windows")).filePath("System32/OpenSSH/ssh.exe");
    if (QFileInfo::exists(path)) return path;
#endif
    return QStandardPaths::findExecutable(QStringLiteral("ssh"));
}
}
BoardController::BoardController(Session *serial, Session *network, QObject *parent)
    : QObject(parent), m_serial(serial), m_network(network)
{
    Q_INIT_RESOURCE(board_scripts);
    for (auto session : {serial, network}) {
        connect(session, &Session::frameReceived, this, [this, session](const QByteArray &b) { frame(session, b); });
        connect(session, &Session::stateChanged, this, [this, session] {
            if (m_busy && m_selected == session && !session->writable()) finish(false, QStringLiteral("数据来源或对端已断开，请确认板端状态"));
        });
    }
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_process.state() != QProcess::NotRunning) { m_abortReason = QStringLiteral("SSH 操作超时，请确认板端状态"); m_process.kill(); }
        else finish(false, QStringLiteral("等待桥接响应超时；检查板端程序、来源和协议配置。操作可能已执行，请勿盲目重发。"));
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_stdout += m_process.readAllStandardOutput();
        if (m_stdout.size() > 131072) { m_abortReason = QStringLiteral("SSH 输出超过容量限制"); m_process.kill(); }
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += m_process.readAllStandardError(); m_stderr = m_stderr.right(32768);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(false, QStringLiteral("SSH 启动失败：")+m_process.errorString());
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
        if (!m_busy) return;
        m_stdout += m_process.readAllStandardOutput(); m_stderr += m_process.readAllStandardError();
        if (!m_abortReason.isEmpty()) { finish(false, m_abortReason); return; }
        if (code != 0 || status != QProcess::NormalExit) {
            finish(false, QStringLiteral("SSH / 板端执行失败：\n")+QString::fromUtf8(m_stderr.right(32768))); return;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(m_stdout, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            finish(false, QStringLiteral("板端未返回合法 JSON；检查 Python 3 和登录输出：\n")+QString::fromUtf8(m_stdout.left(8192))+QString::fromUtf8(m_stderr.right(8192))); return;
        }
        reply(document.object());
    });
}
BoardController::~BoardController()
{ m_process.disconnect(this); if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(1500); } }
void BoardController::finish(bool success, const QString &output)
{ m_timer.stop(); m_busy = false; m_success = success; m_selected = nullptr; m_id = 0; m_output = output.left(32768); emit changed(); }
bool BoardController::sshArguments(const QVariantMap &ssh, QStringList *arguments, bool batch)
{
    const auto host = ssh.value("host").toString().trimmed(), user = ssh.value("user").toString().trimmed();
    const auto key = ssh.value("key").toString().trimmed();
    bool ok = false; const int port = ssh.value("port", 22).toInt(&ok);
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,252}$")).match(host).hasMatch()
        || !QRegularExpression(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}$")).match(user).hasMatch()
        || !ok || port < 1 || port > 65535 || (!key.isEmpty() && !QFileInfo(key).isFile())) {
        finish(false, QStringLiteral("SSH 参数无效：填写主机、用户名、1–65535 端口；密钥路径必须是现有文件")); return false;
    }
    if (sshProgram().isEmpty()) { finish(false, QStringLiteral("未找到 OpenSSH，请启用 Windows OpenSSH 客户端")); return false; }
    *arguments = {"-p", QString::number(port), "-o", "ConnectTimeout=8"};
    if (batch) *arguments << "-o" << "BatchMode=yes" << "-o" << "StrictHostKeyChecking=yes";
    if (!key.isEmpty()) *arguments << "-i" << QDir::toNativeSeparators(key);
    if (!batch) *arguments << "-t";
    *arguments << "--" << user+"@"+host; return true;
}
bool BoardController::execute(int source, const QVariantMap &ssh, const QVariantMap &request)
{
    if (m_busy || m_process.state() != QProcess::NotRunning) return false;
    auto object = QJsonObject::fromVariantMap(request);
    const auto validation = BridgeCodec::validateRequest(object);
    if (!validation.isEmpty()) { finish(false, validation); return false; }
    if (source < 0 || source > 2) { finish(false, QStringLiteral("未知数据来源")); return false; }
    if (m_nextId == 2147483647) m_nextId = 1;
    m_id = m_nextId++; object["id"] = m_id; m_bus = object.value("bus").toString();
    m_selected = source == 0 ? m_serial : source == 1 ? m_network : nullptr;
    QByteArray packet; QString error; QStringList arguments;
    if (m_selected) {
        if (!m_selected->writable() || m_selected->periodic()) {
            finish(false, QStringLiteral("请先在来源页面连接设备/对端，并停止定时发送")); return false;
        }
        m_mode = m_selected->parserMode(); m_head = m_selected->bridgeHead(); m_tail = m_selected->bridgeTail();
        packet = BridgeCodec::encode(object, m_mode, m_head, m_tail, &error);
        if (packet.isEmpty()) { finish(false, error); return false; }
    } else {
        if (!sshArguments(ssh, &arguments, true)) return false;
        QFile script(":/board/board/board_agent.py");
        if (!script.open(QIODevice::ReadOnly)) { finish(false, QStringLiteral("板端参考程序资源缺失")); return false; }
        // 远端 shell 只解释固定命令和 Base64，用户输入通过标准输入 JSON 传递。
        arguments << QStringLiteral("python3 -c 'import base64;exec(base64.b64decode(\"%1\"))'").arg(QString::fromLatin1(script.readAll().toBase64()));
        packet = QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n';
    }
    m_busy = true; m_success = true; m_stdout.clear(); m_stderr.clear(); m_abortReason.clear();
    m_output = QStringLiteral("等待 %1 响应 · ID %2\n%3").arg(source == 0 ? "串口" : source == 1 ? "网络" : "SSH").arg(m_id)
        .arg(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)));
    m_timer.start(source == 2 ? 20000 : 5000); emit changed();
    if (m_selected) {
        if (!m_selected->sendBytes(packet, QStringLiteral("开发板桥接请求 · ID %1").arg(m_id))) {
            finish(false, QStringLiteral("来源发送失败")); return false;
        }
    } else {
        m_process.setProgram(sshProgram()); m_process.setArguments(arguments); m_process.start();
        m_process.write(packet); m_process.closeWriteChannel();
    }
    return true;
}
void BoardController::frame(Session *session, const QByteArray &bytes)
{
    if (!m_busy || session != m_selected) return;
    QString error;
    const auto object = BridgeCodec::decode(bytes, m_mode, m_head, m_tail, &error);
    if (error.isEmpty()) reply(object);
}
void BoardController::reply(const QJsonObject &object)
{
    if (object.value("id").toDouble(-1) != m_id || object.value("bus").toString() != m_bus) return;
    if (!object.value("ok").isBool()) { finish(false, QStringLiteral("桥接响应缺少布尔 ok 字段")); return; }
    finish(object.value("ok").toBool(), QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)));
}
void BoardController::cancel()
{
    if (!m_busy) return;
    if (m_process.state() != QProcess::NotRunning) { m_abortReason = QStringLiteral("已停止等待；板端操作可能已经执行"); m_process.kill(); }
    else finish(false, QStringLiteral("已停止等待；已发送的板端操作无法撤回，请确认板端状态"));
}
bool BoardController::openTerminal(const QVariantMap &ssh)
{
    if (m_busy) return false;
    const bool opened=m_terminal.open(0,ssh);
    finish(opened,m_terminal.status());
    return opened;
}
