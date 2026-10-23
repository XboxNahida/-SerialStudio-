#pragma once
#include <QAbstractListModel>
#include <QDateTime>

struct EventEntry {
    QString stamp, kind, title, detail;
    QByteArray bytes;
    bool valid = true;
};

class EventModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(qulonglong dropped READ dropped NOTIFY countChanged)
public:
    explicit EventModel(int capacity, QObject *parent = nullptr);
    enum Role { StampRole = Qt::UserRole+1, KindRole, TitleRole, DetailRole, HexRole, TextRole, ValidRole };
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void append(QList<EventEntry> entries);
    void clear();
    const QList<EventEntry> &entries() const { return m_entries; }
    qulonglong dropped() const { return m_dropped; }
signals:
    void countChanged();
private:
    int m_capacity;
    qulonglong m_dropped = 0;
    QList<EventEntry> m_entries;
};
