#include "protocol.h"
#include <QJsonDocument>
#include <QJsonParseError>

bool decodeHex(const QString &text, QByteArray *result, QString *error)
{
    if (!result || !error) return false;
    result->clear(); error->clear();
    QByteArray digits;
    for (const auto ch : text) {
        if (ch.isSpace()) continue;
        const ushort c = ch.unicode();
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            *error = QStringLiteral("HEX 只能包含 0–9、A–F 和空白字符"); return false;
        }
        digits.append(char(c));
    }
    if (digits.isEmpty() || digits.size() % 2) {
        *error = QStringLiteral("请输入完整字节，每字节两位 HEX"); return false;
    }
    *result = QByteArray::fromHex(digits);
    return true;
}

quint16 crc16(const QByteArray &bytes)
{
    quint16 crc = 0xffff;
    for (const char ch : bytes) {
        crc ^= quint8(ch);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? quint16((crc >> 1) ^ 0xa001) : quint16(crc >> 1);
    }
    return crc;
}

namespace {
int byte(const QByteArray &b, qsizetype i) { return quint8(b.at(i)); }
int word(const QByteArray &b, qsizetype i) { return byte(b, i) * 256 + byte(b, i+1); }

QList<int> modbusLengths(const QByteArray &b, qsizetype offset)
{
    const auto remaining = b.size() - offset;
    if (remaining < 2 || byte(b, offset) > 247) return {};
    const int fc = byte(b, offset+1);
    if ((fc & 0x80) && (fc & 0x7f) >= 1 && (fc & 0x7f) <= 0x10) return {5};
    if (fc >= 1 && fc <= 4) {
        QList<int> sizes{8};
        if (remaining >= 3 && byte(b, offset+2) > 0 && byte(b, offset+2) <= 250)
            sizes.prepend(byte(b, offset+2) + 5);
        return sizes;
    }
    if (fc == 5 || fc == 6) return {8};
    if (fc == 15 || fc == 16) {
        QList<int> sizes{8};
        if (remaining >= 7 && byte(b, offset+6) <= 246)
            sizes.prepend(byte(b, offset+6) + 9);
        return sizes;
    }
    return {};
}

ParsedFrame describeModbus(const QByteArray &b)
{
    const int fc = byte(b, 1);
    ParsedFrame frame{b, QStringLiteral("Modbus RTU · 0x%1").arg(fc, 2, 16, QLatin1Char('0')), {}, true};
    frame.detail = QStringLiteral("从站 %1 · 功能码 0x%2 · CRC16 正确\n")
        .arg(byte(b, 0)).arg(fc, 2, 16, QLatin1Char('0'));
    if (fc & 0x80) {
        frame.title = QStringLiteral("Modbus 异常响应");
        frame.detail += QStringLiteral("异常码：%1").arg(byte(b, 2));
    } else if (fc >= 1 && fc <= 4 && b.size() == byte(b, 2) + 5
               && (fc <= 2 || byte(b, 2) % 2 == 0)) {
        frame.detail += QStringLiteral("读取响应 · 数据字节：%1").arg(byte(b, 2));
        if (fc == 3 || fc == 4) {
            QStringList values;
            for (qsizetype i = 3; i+1 < b.size()-2; i += 2) values << QString::number(word(b, i));
            frame.detail += QStringLiteral("\n寄存器：") + values.join(QStringLiteral(", "));
        } else {
            frame.detail += QStringLiteral("\n位数据（低位优先）：") + QString::fromLatin1(b.mid(3, byte(b, 2)).toHex(' '));
            if (b.size() == 8)
                frame.detail += QStringLiteral("\n方向歧义：也可能为读取请求，起始地址 %1、数量 %2；请结合业务判断")
                    .arg(word(b, 2)).arg(word(b, 4));
        }
    } else if (b.size() >= 8) {
        frame.detail += QStringLiteral("起始地址：%1 (0x%2)\n%3：%4")
            .arg(word(b, 2)).arg(word(b, 2), 4, 16, QLatin1Char('0'))
            .arg((fc == 5 || fc == 6) ? QStringLiteral("写入值") : QStringLiteral("数量"))
            .arg(word(b, 4));
        if ((fc == 15 || fc == 16) && b.size() > 8)
            frame.detail += QStringLiteral("\n写入数据：") + QString::fromLatin1(b.mid(7, byte(b, 6)).toHex(' '));
    }
    return frame;
}
}

void ProtocolParser::configure(Mode mode, QByteArray head, QByteArray tail)
{ m_mode = mode; m_head = head; m_tail = tail; reset(); }
void ProtocolParser::reset() { m_buffer.clear(); }

QList<ParsedFrame> ProtocolParser::feed(const QByteArray &bytes)
{
    if (bytes.isEmpty()) return {};
    if (m_mode == Raw) return {{bytes.left(65536), QStringLiteral("原始字节流"), QString::fromLatin1(bytes.left(4096).toHex(' ')), true}};
    QList<ParsedFrame> frames;
    // 分段追加，保证即使调用方传入超大块，内部缓存也有明确边界。
    for (qsizetype pos = 0; pos < bytes.size(); pos += 4096) {
        m_buffer += bytes.mid(pos, 4096);
        frames += process(false);
        const qsizetype limit = m_mode == Modbus ? 512 : 65536;
        if (m_buffer.size() > limit) {
            frames.append({m_buffer.left(64), QStringLiteral("缓存超限"), QStringLiteral("不完整帧超过缓存上限，已丢弃并重新同步"), false});
            m_buffer.clear();
        }
    }
    return frames;
}

QList<ParsedFrame> ProtocolParser::flush() { return process(true); }

QList<ParsedFrame> ProtocolParser::process(bool idle)
{
    QList<ParsedFrame> frames;
    if (m_mode == Lines) {
        qsizetype end;
        while ((end = m_buffer.indexOf('\n')) >= 0) {
            auto raw = m_buffer.left(end+1); m_buffer.remove(0, end+1);
            auto content = raw.chopped(1);
            if (content.endsWith('\r')) content.chop(1);
            QJsonParseError error;
            const auto json = QJsonDocument::fromJson(content, &error);
            const bool validJson = error.error == QJsonParseError::NoError && !json.isNull();
            frames.append({raw, validJson ? QStringLiteral("JSON 文本行") : QStringLiteral("文本行"),
                           validJson ? QString::fromUtf8(json.toJson(QJsonDocument::Indented)) : QString::fromUtf8(content), true});
        }
    } else if (m_mode == Custom && !m_head.isEmpty() && !m_tail.isEmpty()) {
        while (!m_buffer.isEmpty()) {
            auto start = m_buffer.indexOf(m_head);
            if (start < 0) {
                // 保留可能横跨下一次接收的帧头前缀。
                qsizetype keep = qMin(m_head.size()-1, m_buffer.size());
                while (keep > 0 && !m_head.startsWith(m_buffer.right(keep))) --keep;
                const auto discard = m_buffer.size()-keep;
                if (discard > 0) {
                    frames.append({m_buffer.left(qMin(discard, qsizetype(64))), QStringLiteral("帧头同步"),
                                   QStringLiteral("跳过 %1 字节非帧数据").arg(discard), false});
                    m_buffer.remove(0, discard);
                }
                break;
            }
            if (start > 0) {
                frames.append({m_buffer.left(qMin(start, qsizetype(64))), QStringLiteral("帧头同步"), QStringLiteral("跳过 %1 字节").arg(start), false});
                m_buffer.remove(0, start);
            }
            const auto end = m_buffer.indexOf(m_tail, m_head.size());
            if (end < 0) break;
            const auto count = end + m_tail.size();
            auto raw = m_buffer.left(count); m_buffer.remove(0, count);
            frames.append({raw, QStringLiteral("自定义完整帧"),
                           QStringLiteral("帧长 %1 字节\n负载：%2").arg(count)
                           .arg(QString::fromLatin1(raw.mid(m_head.size(), end-m_head.size()).toHex(' '))), true});
        }
    } else if (m_mode == Modbus) {
        while (m_buffer.size() >= 5) {
            qsizetype found = -1; int length = 0;
            for (qsizetype offset = 0; offset+5 <= m_buffer.size() && found < 0; ++offset) {
                for (const int size : modbusLengths(m_buffer, offset)) {
                    if (offset+size <= m_buffer.size() && crc16(m_buffer.mid(offset, size)) == 0) {
                        found = offset; length = size; break;
                    }
                }
            }
            if (found < 0) break;
            if (found > 0) frames.append({m_buffer.left(found), QStringLiteral("Modbus 重新同步"), QStringLiteral("跳过无效数据或 CRC 错误帧"), false});
            m_buffer.remove(0, found);
            frames.append(describeModbus(m_buffer.left(length)));
            m_buffer.remove(0, length);
        }
        if (idle && !m_buffer.isEmpty()) {
            frames.append({m_buffer, QStringLiteral("Modbus 校验失败"), QStringLiteral("CRC 错误、帧不完整或不支持的功能码"), false});
            m_buffer.clear();
        }
    }
    return frames;
}
