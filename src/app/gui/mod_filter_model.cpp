#include "app/gui/mod_filter_model.hpp"

#include "app/core/qt_util.hpp"
#include "app/core/view_data.hpp"
#include "app/gui/mod_list_model.hpp"
#include "core/playset_ops.hpp"

#include <iterator>

namespace pmm::app {
namespace {

bool isMissingStatus(const QString& status) {
    return status == QLatin1String("missing") || status == QLatin1String("pending") ||
           status == QLatin1String("downloading") || status == QLatin1String("failed") ||
           status == QLatin1String("unavailable") || status == QLatin1String("needs_user_action");
}

// Browse order of the status column: problems first.
int statusRank(const QString& status) {
    static const char* const order[] = {"broken",  "failed",  "unavailable", "needs_user_action",
                                        "missing", "pending", "downloading", "ready"};
    for (int i = 0; i < static_cast<int>(std::size(order)); ++i) {
        if (status == QLatin1String(order[i]))
            return i;
    }
    return static_cast<int>(std::size(order));
}

} // namespace

ModFilterModel::ModFilterModel(ModListModel* source, QObject* parent)
    : QSortFilterProxyModel(parent), m_source(source) {
    setSourceModel(source);
    setDynamicSortFilter(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    updateReorder();
}

void ModFilterModel::setSearch(const QString& search) {
    if (search == m_search)
        return;
    m_search = search;
    const QString needle = search.trimmed();
    const bool refilterNeeded = needle != m_needle;
    m_needle = needle;
    emit searchChanged();
    if (refilterNeeded)
        refilter();
    updateReorder();
}

void ModFilterModel::setStatusFilter(const QString& filter) {
    static const QStringList known{QStringLiteral("all"), QStringLiteral("enabled"),
                                   QStringLiteral("disabled"), QStringLiteral("missing"),
                                   QStringLiteral("warning")};
    const QString normalized = known.contains(filter) ? filter : QStringLiteral("all");
    if (normalized == m_status)
        return;
    m_status = normalized;
    emit statusFilterChanged();
    refilter();
    updateReorder();
}

void ModFilterModel::setSortMode(const QString& mode) {
    static const QStringList known{QStringLiteral("order"), QStringLiteral("name"),
                                   QStringLiteral("source"), QStringLiteral("status")};
    const QString normalized = known.contains(mode) ? mode : QStringLiteral("order");
    if (normalized == m_sort)
        return;
    m_sort = normalized;
    emit sortModeChanged();
    if (m_sort == QLatin1String("order")) {
        sort(-1); // source (load) order
    } else {
        invalidate();
        sort(0);
    }
    updateReorder();
}

int ModFilterModel::sourceRow(int proxyRowIndex) const {
    if (proxyRowIndex < 0 || proxyRowIndex >= rowCount())
        return -1;
    return mapToSource(index(proxyRowIndex, 0)).row();
}

int ModFilterModel::proxyRow(int sourceRowIndex) const {
    if (sourceRowIndex < 0 || sourceRowIndex >= m_source->rowCount())
        return -1;
    return mapFromSource(m_source->index(sourceRowIndex, 0)).row();
}

bool ModFilterModel::filterAcceptsRow(int sourceRowIndex, const QModelIndex& sourceParent) const {
    if (sourceParent.isValid())
        return false;
    const ModRowData* row = m_source->row(sourceRowIndex);
    if (!row)
        return false;
    if (!m_needle.isEmpty() && !row->name.contains(m_needle, Qt::CaseInsensitive) &&
        !row->providerId.contains(m_needle, Qt::CaseInsensitive))
        return false;
    if (m_status == QLatin1String("enabled"))
        return row->entry.enabled;
    if (m_status == QLatin1String("disabled"))
        return !row->entry.enabled;
    if (m_status == QLatin1String("missing"))
        return isMissingStatus(row->status);
    if (m_status == QLatin1String("warning"))
        return row->status == QLatin1String("broken") || !row->warnings.isEmpty();
    return true;
}

bool ModFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const ModRowData* a = m_source->row(left.row());
    const ModRowData* b = m_source->row(right.row());
    if (!a || !b)
        return left.row() < right.row();
    int order = 0;
    if (m_sort == QLatin1String("name")) {
        order = a->name.compare(b->name, Qt::CaseInsensitive);
    } else if (m_sort == QLatin1String("source")) {
        order = a->sourceLabel.compare(b->sourceLabel, Qt::CaseInsensitive);
        if (order == 0)
            order = a->name.compare(b->name, Qt::CaseInsensitive);
    } else if (m_sort == QLatin1String("status")) {
        order = statusRank(a->status) - statusRank(b->status);
        if (order == 0)
            order = a->name.compare(b->name, Qt::CaseInsensitive);
    }
    if (order != 0)
        return order < 0;
    return left.row() < right.row(); // stable: load order breaks ties
}

void ModFilterModel::refilter() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
}

void ModFilterModel::updateReorder() {
    const ReorderPolicy policy = reorderPolicy(queries::toViewFilter(m_search, m_status, m_sort));
    const QString reason = qs(policy.explanation);
    if (policy.allowed == m_reorderAllowed && reason == m_reorderReason)
        return;
    m_reorderAllowed = policy.allowed;
    m_reorderReason = reason;
    emit reorderChanged();
}

} // namespace pmm::app
