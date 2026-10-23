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
    case EventIdRole: return m_ids[index.row()];
    case EventSelectedRole: return m_selected.contains(m_ids[index.row()]);
    default: return {};
    }
}
QHash<int, QByteArray> EventModel::roleNames() const
{ return {{StampRole,"stamp"},{KindRole,"kind"},{TitleRole,"heading"},{DetailRole,"detail"},{HexRole,"hexData"},{TextRole,"textData"},{ValidRole,"valid"},{EventIdRole,"eventId"},{EventSelectedRole,"eventSelected"}}; }
void EventModel::append(QList<EventEntry> entries)
{
    if (entries.isEmpty()) return;
    if (entries.size() > m_capacity) {
        const auto overflow = entries.size()-m_capacity;
        m_dropped += qulonglong(overflow); entries.remove(0, overflow);
    }
    const int removeCount = qMax(0, int(m_entries.size()+entries.size())-m_capacity);
    bool selectionRemoved = false;
    if (removeCount) {
        beginRemoveRows({}, 0, removeCount-1);
        for (int i = 0; i < removeCount; ++i) selectionRemoved |= m_selected.remove(m_ids[i]);
        m_ids.remove(0, removeCount);
        m_entries.remove(0, removeCount); m_dropped += qulonglong(removeCount);
        endRemoveRows();
    }
    beginInsertRows({}, int(m_entries.size()), int(m_entries.size()+entries.size()-1));
    for (qsizetype i = 0; i < entries.size(); ++i) m_ids.append(QString::number(++m_nextId));
    m_entries += entries;
    endInsertRows(); emit countChanged();
    if (selectionRemoved) emit selectionChanged();
}
void EventModel::clear()
{
    beginResetModel(); m_entries.clear(); m_ids.clear(); m_selected.clear(); m_dropped = 0; endResetModel();
    emit countChanged(); emit selectionChanged();
}
bool EventModel::setEventSelected(const QString &id, bool selected)
{
    const auto row = m_ids.indexOf(id);
    if (row < 0) return false;
    if (m_selected.contains(id) == selected) return true;
    if (selected) m_selected.insert(id); else m_selected.remove(id);
    emit dataChanged(index(int(row)), index(int(row)), {EventSelectedRole});
    emit selectionChanged();
    return true;
}
void EventModel::selectAll()
{
    if (m_selected.size() == m_ids.size()) return;
    m_selected = QSet<QString>(m_ids.begin(), m_ids.end());
    emit dataChanged(index(0), index(rowCount()-1), {EventSelectedRole});
    emit selectionChanged();
}
void EventModel::clearSelection()
{
    if (m_selected.isEmpty()) return;
    m_selected.clear();
    if (!m_entries.isEmpty()) emit dataChanged(index(0), index(rowCount()-1), {EventSelectedRole});
    emit selectionChanged();
}
QString EventModel::selectedText(bool hex, bool detailed, bool protocol) const
{
    QStringList lines;
    for (int row = 0; row < rowCount(); ++row) {
        if (!m_selected.contains(m_ids[row])) continue;
        const auto &entry = m_entries[row];
        if (protocol) {
            QString title = entry.title;
            if (entry.kind == "DEMO") title += QStringLiteral(" · 离线演示");
            lines.append(detailed ? entry.stamp + " " + title : title);
            if (!entry.detail.isEmpty()) lines.append(entry.detail);
            if (detailed && !entry.bytes.isEmpty()) lines.append(data(index(row), HexRole).toString());
        } else {
            lines.append(detailed ? entry.stamp + " " + entry.kind : entry.kind);
            if (detailed || (entry.kind != "RX" && entry.kind != "TX") || !entry.valid) {
                const auto title = entry.title + (entry.detail.isEmpty() ? QString() : QStringLiteral(" · ") + entry.detail);
                if (!title.isEmpty()) lines.append(title);
            }
            if (!entry.bytes.isEmpty()) lines.append(data(index(row), hex ? HexRole : TextRole).toString());
        }
    }
    return lines.join('\n');
}
