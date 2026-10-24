#pragma once
#include <QAbstractListModel>
#include <QDateTime>
#include <QSet>

struct EventEntry {
    QString stamp, kind, title, detail;
    QByteArray bytes;
    bool valid = true;
};

class EventModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(qulonglong dropped READ dropped NOTIFY countChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
public:
    explicit EventModel(int capacity, QObject *parent = nullptr);
    enum Role { StampRole = Qt::UserRole+1, KindRole, TitleRole, DetailRole, HexRole, TextRole, ValidRole, EventIdRole, EventSelectedRole };
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void append(QList<EventEntry> entries);
    void clear();
    const QList<EventEntry> &entries() const { return m_entries; }
    qulonglong dropped() const { return m_dropped; }
    int selectedCount() const { return int(m_selected.size()); }
    Q_INVOKABLE bool setEventSelected(const QString &id, bool selected);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QString selectedText(bool hex, bool detailed, bool protocol) const;
    Q_INVOKABLE QString continuousText(bool hex) const;
    Q_INVOKABLE QVariantList continuousEntries(bool hex) const;
signals:
    void countChanged();
    void selectionChanged();
private:
    int m_capacity;
    qulonglong m_dropped = 0;
    QList<EventEntry> m_entries;
    QList<QString> m_ids;
    QSet<QString> m_selected;
    qulonglong m_nextId = 0;
};
