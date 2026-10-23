#pragma once
#include <QByteArray>
#include <QVariantMap>
#include <QJsonArray>

struct DataResult {
    QVariantMap values;
    QString text, error;
};

class PayloadDecoder {
public:
    bool configure(const QString &format, const QString &schema, QString *error);
    DataResult decode(const QByteArray &payload,qsizetype limit=65536) const;
private:
    QString m_format = "utf8";
    QJsonArray m_fields;
};
