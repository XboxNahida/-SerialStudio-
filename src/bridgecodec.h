#pragma once
#include <QJsonObject>
#include "protocol.h"

namespace BridgeCodec {
QByteArray encode(const QJsonObject &object, int mode, const QByteArray &head, const QByteArray &tail, QString *error);
QJsonObject decode(const QByteArray &frame, int mode, const QByteArray &head, const QByteArray &tail, QString *error);
QString validateRequest(const QJsonObject &request);
}
