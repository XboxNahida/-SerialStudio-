#include "eventmodel.h"

EventModel::EventModel(int capacity, QObject *parent) : QAbstractListModel(parent), m_capacity(qMax(1, capacity)) {}
int EventModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_entries.size()); }
QVariant EventModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) return {};
    const auto &e = m_entries[index.row()];
    switch (role) {
    case StampRole: return e.stamp;
    case KindRole: return e.kind;
    case TitleRole: return e.title;
    case DetailRole: return e.detail;
    case HexRole: return QString::fromLatin1(e.bytes.toHex(' ')).toUpper();
    case TextRole: return QString::fromUtf8(e.bytes).replace(QChar::Null, QChar(0x2400));
    case ValidRole: return e.valid;
    default: return {};
    }
}
QHash<int, QByteArray> EventModel::roleNames() const
{ return {{StampRole,"stamp"},{KindRole,"kind"},{TitleRole,"heading"},{DetailRole,"detail"},{HexRole,"hexData"},{TextRole,"textData"},{ValidRole,"valid"}}; }
void EventModel::append(QList<EventEntry> entries)
{
    if (entries.isEmpty()) return;
    if (entries.size() > m_capacity) {
        const auto overflow = entries.size()-m_capacity;
        m_dropped += qulonglong(overflow); entries.remove(0, overflow);
    }
    const int removeCount = qMax(0, int(m_entries.size()+entries.size())-m_capacity);
    if (removeCount) {
        beginRemoveRows({}, 0, removeCount-1);
        m_entries.remove(0, removeCount); m_dropped += qulonglong(removeCount);
        endRemoveRows();
    }
    beginInsertRows({}, int(m_entries.size()), int(m_entries.size()+entries.size()-1));
    m_entries += entries;
    endInsertRows(); emit countChanged();
}
void EventModel::clear()
{ beginResetModel(); m_entries.clear(); m_dropped = 0; endResetModel(); emit countChanged(); }
