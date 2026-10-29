#pragma once

// Playsets of the current game, the active one, and the undo path for
// deletions. canRestore and history() are scoped to the current game;
// canRestore is true while a playset deleted in this session can be brought
// back with restoreLast(). Management requests run on the services worker:
// the int/bool results only say whether a request was accepted, the outcome
// (new active playset, or an error notification) follows asynchronously.

#include "app/core/view_data.hpp"

#include <QAbstractListModel>
#include <QVariantList>

#include <cstdint>
#include <set>
#include <vector>

namespace pmm::app {

class Backend;

class PlaysetsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int activeId READ activeId NOTIFY activeChanged)
    Q_PROPERTY(QString activeName READ activeName NOTIFY activeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool canRestore READ canRestore NOTIFY canRestoreChanged)

public:
    enum Role {
        PlaysetIdRole = Qt::UserRole + 1,
        NameRole,
        EntryCountRole,
        EnabledCountRole,
        UpdatedTextRole,
    };

    explicit PlaysetsModel(Backend* backend, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int activeId() const { return static_cast<int>(m_activeId); }
    QString activeName() const;
    int count() const { return static_cast<int>(m_rows.size()); }
    bool canRestore() const { return m_canRestore; }

    Q_INVOKABLE void activate(int id);
    Q_INVOKABLE int create(const QString& name);
    Q_INVOKABLE int duplicate(int id, const QString& name);
    Q_INVOKABLE bool rename(int id, const QString& name);
    Q_INVOKABLE bool remove(int id);
    Q_INVOKABLE bool restoreLast();
    Q_INVOKABLE QVariantList history() const;
    Q_INVOKABLE bool restoreHistory(int historyId);

    // Backend side.
    void setList(const PlaysetListData& list);
    void setActive(std::int64_t id);
    void updateCounts(std::int64_t id, int entryCount, int enabledCount, const QString& name);
    void rememberDeleted(std::int64_t historyId);
    void clear();
    const std::vector<PlaysetSummaryData>& rows() const { return m_rows; }

signals:
    void activeChanged();
    void countChanged();
    void canRestoreChanged();

private:
    void setCanRestore(bool value);
    int rowOf(std::int64_t id) const;

    Backend* m_backend;
    std::vector<PlaysetSummaryData> m_rows;
    std::vector<HistoryData> m_history;
    std::int64_t m_activeId = -1;
    std::optional<std::int64_t> m_lastDeleted;
    std::set<std::int64_t> m_deletedThisSession; // history ids
    bool m_canRestore = false;
};

} // namespace pmm::app
