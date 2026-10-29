#include "app/gui/jobs_model.hpp"

#include <algorithm>

namespace pmm::app {
namespace {

bool sameRow(const JobRowData& a, const JobRowData& b) {
    return a.jobId == b.jobId && a.name == b.name && a.providerId == b.providerId &&
           a.state == b.state && a.stateText == b.stateText && a.progress == b.progress &&
           a.errorMessage == b.errorMessage && a.canRetry == b.canRetry &&
           a.canOpenFallback == b.canOpenFallback;
}

} // namespace

JobsModel::JobsModel(QObject* parent) : QAbstractListModel(parent) {}

int JobsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant JobsModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const JobRowData& row = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case JobIdRole:
        return static_cast<int>(row.jobId);
    case Qt::DisplayRole:
    case NameRole:
        return row.name;
    case ProviderIdRole:
        return row.providerId;
    case StateRole:
        return row.state;
    case StateTextRole:
        return row.stateText;
    case ProgressRole:
        return row.progress;
    case ErrorMessageRole:
        return row.errorMessage;
    case CanRetryRole:
        return row.canRetry;
    case CanOpenFallbackRole:
        return row.canOpenFallback;
    default:
        return {};
    }
}

QHash<int, QByteArray> JobsModel::roleNames() const {
    return {{JobIdRole, "jobId"},
            {NameRole, "name"},
            {ProviderIdRole, "providerId"},
            {StateRole, "state"},
            {StateTextRole, "stateText"},
            {ProgressRole, "progress"},
            {ErrorMessageRole, "errorMessage"},
            {CanRetryRole, "canRetry"},
            {CanOpenFallbackRole, "canOpenFallback"}};
}

void JobsModel::setRows(std::vector<JobRowData> rows) {
    const int before = count();
    // Drop jobs that are gone.
    for (int i = count() - 1; i >= 0; --i) {
        const std::int64_t id = m_rows[static_cast<std::size_t>(i)].jobId;
        if (std::none_of(rows.begin(), rows.end(),
                         [&](const JobRowData& r) { return r.jobId == id; })) {
            beginRemoveRows({}, i, i);
            m_rows.erase(m_rows.begin() + i);
            endRemoveRows();
        }
    }
    // Update or insert in the snapshot's order.
    for (std::size_t target = 0; target < rows.size(); ++target) {
        const std::int64_t id = rows[target].jobId;
        const auto found =
            std::find_if(m_rows.begin() + static_cast<std::ptrdiff_t>(target), m_rows.end(),
                         [&](const JobRowData& r) { return r.jobId == id; });
        if (found == m_rows.end()) {
            beginInsertRows({}, static_cast<int>(target), static_cast<int>(target));
            m_rows.insert(m_rows.begin() + static_cast<std::ptrdiff_t>(target), rows[target]);
            endInsertRows();
            continue;
        }
        const int from = static_cast<int>(found - m_rows.begin());
        if (from != static_cast<int>(target)) {
            beginMoveRows({}, from, from, {}, static_cast<int>(target));
            std::rotate(m_rows.begin() + static_cast<std::ptrdiff_t>(target), found, found + 1);
            endMoveRows();
        }
        if (!sameRow(m_rows[target], rows[target])) {
            m_rows[target] = rows[target];
            const QModelIndex changed = index(static_cast<int>(target));
            emit dataChanged(changed, changed);
        }
    }
    if (before != count())
        emit countChanged();
}

} // namespace pmm::app
