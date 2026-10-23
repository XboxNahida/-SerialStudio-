#pragma once
#include <QByteArray>
#include <QList>
#include <QString>

struct ParsedFrame {
    QByteArray bytes;
    QString title;
    QString detail;
    bool valid = true;
};

bool decodeHex(const QString &text, QByteArray *result, QString *error);
quint16 crc16(const QByteArray &bytes);

class ProtocolParser {
public:
    enum Mode { Raw, Lines, Modbus, Custom };
    void configure(Mode mode, QByteArray head = {}, QByteArray tail = {});
    QList<ParsedFrame> feed(const QByteArray &bytes);
    QList<ParsedFrame> flush();
    void reset();
    qsizetype bufferedSize() const { return m_buffer.size(); }
private:
    QList<ParsedFrame> process(bool idle);
    Mode m_mode = Raw;
    QByteArray m_buffer;
    QByteArray m_head;
    QByteArray m_tail;
};
