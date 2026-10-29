#include "app/gui/inventory_model.hpp"

#include "app/gui/backend.hpp"

namespace pmm::app {

InventoryModel::InventoryModel(Backend* backend, QObject* parent)
    : QAbstractListModel(parent), m_backend(backend) {}

int InventoryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant InventoryModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const InventoryRowData& row = m_rows[m_visible[static_cast<std::size_t>(index.row())]];
    switch (role) {
    case IdentityKeyRole:
        return row.key;
    case Qt::DisplayRole:
    case NameRole:
        return row.name;
    case SourceRole:
        return row.source;
    case SourceLabelRole:
        return row.sourceLabel;
    case ProviderIdRole:
        return row.providerId;
    case VersionRole:
        return row.version;
    case InPlaysetRole:
        return m_inPlayset.contains(row.key);
    case StateTextRole:
        return row.stateText;
    case ProblemsRole:
        return row.problems;
    default:
        return {};
    }
}

QHash<int, QByteArray> InventoryModel::roleNames() const {
    return {{IdentityKeyRole, "identityKey"}, {NameRole, "name"},
            {SourceRole, "source"},           {SourceLabelRole, "sourceLabel"},
            {ProviderIdRole, "providerId"},   {VersionRole, "version"},
            {InPlaysetRole, "inPlayset"},     {StateTextRole, "stateText"},
            {ProblemsRole, "problems"}};
}

void InventoryModel::setFilter(const QString& filter) {
    if (filter == m_filter)
        return;
    m_filter = filter;
    emit filterChanged();
    rebuild();
}

void InventoryModel::refresh() {
    m_backend->refreshInventory();
}

void InventoryModel::setRows(std::vector<InventoryRowData> rows) {
    m_rows = std::move(rows);
    rebuild();
}

void InventoryModel::setPlaysetKeys(QSet<QString> keys) {
    if (keys == m_inPlayset)
        return;
    const QSet<QString> before = std::exchange(m_inPlayset, std::move(keys));
    for (std::size_t i = 0; i < m_visible.size(); ++i) {
        const QString& key = m_rows[m_visible[i]].key;
        if (before.contains(key) != m_inPlayset.contains(key)) {
            const QModelIndex changed = index(static_cast<int>(i));
            emit dataChanged(changed, changed, {InPlaysetRole});
        }
    }
}

void InventoryModel::rebuild() {
    const int before = count();
    const QString needle = m_filter.trimmed();
    beginResetModel();
    m_visible.clear();
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        const InventoryRowData& row = m_rows[i];
        if (needle.isEmpty() || row.name.contains(needle, Qt::CaseInsensitive) ||
            row.providerId.contains(needle, Qt::CaseInsensitive))
            m_visible.push_back(i);
    }
    endResetModel();
    if (before != count())
        emit countChanged();
}

} // namespace pmm::app
