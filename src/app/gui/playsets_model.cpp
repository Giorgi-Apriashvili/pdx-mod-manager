#include "app/gui/playsets_model.hpp"

#include "app/gui/backend.hpp"

#include <algorithm>

namespace pmm::app {
namespace {

bool sameRow(const PlaysetSummaryData& a, const PlaysetSummaryData& b) {
    return a.id == b.id && a.name == b.name && a.entryCount == b.entryCount &&
           a.enabledCount == b.enabledCount && a.updatedText == b.updatedText;
}

} // namespace

PlaysetsModel::PlaysetsModel(Backend* backend, QObject* parent)
    : QAbstractListModel(parent), m_backend(backend) {}

int PlaysetsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant PlaysetsModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const PlaysetSummaryData& row = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return row.name;
    case PlaysetIdRole:
        return static_cast<int>(row.id);
    case EntryCountRole:
        return row.entryCount;
    case EnabledCountRole:
        return row.enabledCount;
    case UpdatedTextRole:
        return row.updatedText;
    default:
        return {};
    }
}

QHash<int, QByteArray> PlaysetsModel::roleNames() const {
    return {{PlaysetIdRole, "playsetId"},
            {NameRole, "name"},
            {EntryCountRole, "entryCount"},
            {EnabledCountRole, "enabledCount"},
            {UpdatedTextRole, "updatedText"}};
}

QString PlaysetsModel::activeName() const {
    const int row = rowOf(m_activeId);
    return row >= 0 ? m_rows[static_cast<std::size_t>(row)].name : QString();
}

void PlaysetsModel::activate(int id) {
    if (id < 0 || rowOf(id) < 0 || id == m_activeId)
        return;
    m_backend->activatePlayset(id);
}

int PlaysetsModel::create(const QString& name) {
    m_backend->createPlayset(name, std::nullopt);
    return -1;
}

int PlaysetsModel::duplicate(int id, const QString& name) {
    if (rowOf(id) < 0)
        return -1;
    m_backend->createPlayset(name, id);
    return -1;
}

bool PlaysetsModel::rename(int id, const QString& name) {
    if (rowOf(id) < 0 || name.trimmed().isEmpty())
        return false;
    m_backend->renamePlayset(id, name.trimmed());
    return true;
}

bool PlaysetsModel::remove(int id) {
    if (rowOf(id) < 0)
        return false;
    m_backend->removePlayset(id);
    return true;
}

bool PlaysetsModel::restoreLast() {
    if (!m_lastDeleted)
        return false;
    m_backend->restoreHistory(*m_lastDeleted, true);
    return true;
}

QVariantList PlaysetsModel::history() const {
    QVariantList out;
    for (const HistoryData& entry : m_history) {
        out.push_back(QVariantMap{{QStringLiteral("historyId"), static_cast<int>(entry.historyId)},
                                  {QStringLiteral("reason"), entry.reason},
                                  {QStringLiteral("playsetName"), entry.playsetName},
                                  {QStringLiteral("createdText"), entry.createdText}});
    }
    return out;
}

bool PlaysetsModel::restoreHistory(int historyId) {
    const bool known = std::any_of(m_history.begin(), m_history.end(),
                                   [&](const HistoryData& h) { return h.historyId == historyId; });
    if (!known)
        return false;
    m_backend->restoreHistory(historyId, false);
    return true;
}

void PlaysetsModel::setList(const PlaysetListData& list) {
    const bool sameIds = list.playsets.size() == m_rows.size() &&
                         std::equal(list.playsets.begin(), list.playsets.end(), m_rows.begin(),
                                    [](const PlaysetSummaryData& a, const PlaysetSummaryData& b) {
                                        return a.id == b.id;
                                    });
    if (!sameIds) {
        beginResetModel();
        m_rows = list.playsets;
        endResetModel();
        emit countChanged();
    } else {
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            if (sameRow(m_rows[i], list.playsets[i]))
                continue;
            m_rows[i] = list.playsets[i];
            const QModelIndex changed = index(static_cast<int>(i));
            emit dataChanged(changed, changed);
        }
    }
    m_history = list.history;
    m_lastDeleted = list.lastDeletedHistoryId;
    setCanRestore(m_lastDeleted && m_deletedThisSession.count(*m_lastDeleted) != 0);
    m_activeId = -2; // force the notification below
    setActive(list.activeId);
}

void PlaysetsModel::setActive(std::int64_t id) {
    if (m_activeId == id)
        return;
    m_activeId = id;
    emit activeChanged();
}

void PlaysetsModel::updateCounts(std::int64_t id, int entryCount, int enabledCount,
                                 const QString& name) {
    const int row = rowOf(id);
    if (row < 0)
        return;
    PlaysetSummaryData& summary = m_rows[static_cast<std::size_t>(row)];
    if (summary.entryCount == entryCount && summary.enabledCount == enabledCount &&
        (name.isEmpty() || summary.name == name))
        return;
    summary.entryCount = entryCount;
    summary.enabledCount = enabledCount;
    if (!name.isEmpty() && summary.name != name) {
        summary.name = name;
        if (id == m_activeId)
            emit activeChanged();
    }
    const QModelIndex changed = index(row);
    emit dataChanged(changed, changed);
}

void PlaysetsModel::rememberDeleted(std::int64_t historyId) {
    m_deletedThisSession.insert(historyId);
}

void PlaysetsModel::clear() {
    if (!m_rows.empty()) {
        beginResetModel();
        m_rows.clear();
        endResetModel();
        emit countChanged();
    }
    m_history.clear();
    m_lastDeleted.reset();
    setCanRestore(false);
    setActive(-1);
}

void PlaysetsModel::setCanRestore(bool value) {
    if (m_canRestore == value)
        return;
    m_canRestore = value;
    emit canRestoreChanged();
}

int PlaysetsModel::rowOf(std::int64_t id) const {
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].id == id)
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace pmm::app
