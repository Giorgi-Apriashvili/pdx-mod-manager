#pragma once

// The center list's view of ModListModel: search (name or provider id, case
// insensitive), status filter and browse sorting. Browsing never changes the
// load order. Reordering is only allowed in the unfiltered load-order view;
// reorderAllowed / reorderBlockedReason come from core reorderPolicy(), the
// same rule PlaysetService enforces.

#include <QSortFilterProxyModel>
#include <QString>

namespace pmm::app {

class ModListModel;

class ModFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(
        QString statusFilter READ statusFilter WRITE setStatusFilter NOTIFY statusFilterChanged)
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(bool reorderAllowed READ reorderAllowed NOTIFY reorderChanged)
    Q_PROPERTY(QString reorderBlockedReason READ reorderBlockedReason NOTIFY reorderChanged)

public:
    explicit ModFilterModel(ModListModel* source, QObject* parent = nullptr);

    QString search() const { return m_search; }
    void setSearch(const QString& search);
    QString statusFilter() const { return m_status; }
    void setStatusFilter(const QString& filter);
    QString sortMode() const { return m_sort; }
    void setSortMode(const QString& mode);
    bool reorderAllowed() const { return m_reorderAllowed; }
    QString reorderBlockedReason() const { return m_reorderReason; }

    Q_INVOKABLE int sourceRow(int proxyRow) const;
    Q_INVOKABLE int proxyRow(int sourceRow) const;

signals:
    void searchChanged();
    void statusFilterChanged();
    void sortModeChanged();
    void reorderChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    void refilter();
    void updateReorder();

    ModListModel* m_source;
    QString m_search;
    QString m_needle; // trimmed search
    QString m_status = QStringLiteral("all");
    QString m_sort = QStringLiteral("order");
    bool m_reorderAllowed = true;
    QString m_reorderReason;
};

} // namespace pmm::app
