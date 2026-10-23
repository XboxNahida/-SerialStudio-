#include "bridgecodec.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>

QByteArray BridgeCodec::encode(const QJsonObject &object, int mode, const QByteArray &head, const QByteArray &tail, QString *error)
{
    error->clear();
    const auto body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (mode == ProtocolParser::Lines) return body + '\n';
    if (mode == ProtocolParser::Custom && !head.isEmpty() && !tail.isEmpty() && head.size() <= 32 && tail.size() <= 32) {
        if (body.contains(tail)) { *error = QStringLiteral("JSON 内容包含当前帧尾，请改用文本行或不冲突的帧尾"); return {}; }
        return head + body + tail;
    }
    *error = QStringLiteral("桥接请在协议页选择文本行 / JSON，或配置自定义帧头尾并应用"); return {};
}
QJsonObject BridgeCodec::decode(const QByteArray &frame, int mode, const QByteArray &head, const QByteArray &tail, QString *error)
{
    error->clear(); QByteArray body = frame;
    if (mode == ProtocolParser::Custom) {
        if (head.isEmpty() || tail.isEmpty() || !frame.startsWith(head) || !frame.endsWith(tail)
            || frame.size() < head.size()+tail.size()) { *error = QStringLiteral("桥接帧头尾不匹配"); return {}; }
        body = frame.mid(head.size(), frame.size()-head.size()-tail.size());
    } else if (mode != ProtocolParser::Lines) { *error = QStringLiteral("当前解析模式不支持 JSON 桥接"); return {}; }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("桥接内容不是 JSON 对象"); return {};
    }
    return document.object();
}
QString BridgeCodec::validateRequest(const QJsonObject &r)
{
    const auto bus = r.value("bus").toString(), op = r.value("op").toString();
    auto number = [&r](const char *name, int low, int high) {
        const auto v = r.value(QLatin1String(name)); const double n = v.toDouble(-1);
        return v.isDouble() && n >= low && n <= high && std::floor(n) == n;
    };
    if ((bus == "system" && op == "info") || (bus == "usb" && op == "list")) return {};
    QByteArray tx; QString error;
    const auto text = r.value("tx").toString();
    if ((!text.trimmed().isEmpty() && !decodeHex(text, &tx, &error)) || tx.size() > 4096)
        return QStringLiteral("HEX 数据无效或超过 4096 字节");
    if (!number("read", 0, 4096)) return QStringLiteral("读取长度范围为 0–4096");
    if (bus == "i2c" && op == "transfer") {
        if (!number("device", 0, 255) || !number("address", 0, 127) || (tx.isEmpty() && r.value("read").toInt() == 0))
            return QStringLiteral("检查 I²C 总线编号、7 位地址和读写长度");
        return {};
    }
    if (bus == "spi" && op == "transfer") {
        if (!QRegularExpression(QStringLiteral("^/dev/spidev[0-9]{1,3}\\.[0-9]{1,3}$")).match(r.value("device").toString()).hasMatch()
            || !number("mode", 0, 3) || !number("speed", 1, 100000000) || tx.isEmpty())
            return QStringLiteral("检查 SPI 设备、模式、频率及发送数据；读取需发送 FF 等字节产生时钟");
        return {};
    }
    if (bus == "usb" && (op == "read" || op == "write")) {
        if (!number("vid", 0, 65535) || !number("pid", 0, 65535) || !number("interface", 0, 255)
            || !number("endpoint", 1, 255) || !number("timeout", 100, 5000)
            || !number("usb_bus", 0, 255) || !number("usb_address", 0, 255))
            return QStringLiteral("检查 USB 标识、接口、端点和超时");
        const int endpoint = r.value("endpoint").toInt();
        if ((endpoint & 0x70) || !(endpoint & 0x0f) || (op == "read" && (!(endpoint & 0x80) || r.value("read").toInt() == 0))
            || (op == "write" && ((endpoint & 0x80) || tx.isEmpty())))
            return QStringLiteral("USB 读取需 IN 端点和正数长度，写入需 OUT 端点和非空数据");
        return {};
    }
    return QStringLiteral("不支持的总线操作");
}
