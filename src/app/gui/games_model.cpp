#include "app/gui/games_model.hpp"

#include "app/gui/backend.hpp"

namespace pmm::app {
namespace {

bool sameRow(const GameRowData& a, const GameRowData& b) {
    return a.key == b.key && a.name == b.name && a.supported == b.supported &&
           a.installed == b.installed && a.manualLocation == b.manualLocation &&
           a.statusText == b.statusText && a.version == b.version &&
           a.installPath == b.installPath && a.userDataPath == b.userDataPath;
}

} // namespace

GamesModel::GamesModel(Backend* backend, QObject* parent)
    : QAbstractListModel(parent), m_backend(backend) {}

int GamesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant GamesModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const GameRowData& row = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return row.name;
    case KeyRole:
        return row.key;
    case SupportedRole:
        return row.supported;
    case InstalledRole:
        return row.installed;
    case StatusTextRole:
        return row.statusText;
    case VersionRole:
        return row.version;
    case InstallPathRole:
        return row.installPath;
    case UserDataPathRole:
        return row.userDataPath;
    case ManualLocationRole:
        return row.manualLocation;
    default:
        return {};
    }
}

QHash<int, QByteArray> GamesModel::roleNames() const {
    return {{KeyRole, "key"},
            {NameRole, "name"},
            {SupportedRole, "supported"},
            {InstalledRole, "installed"},
            {StatusTextRole, "statusText"},
            {VersionRole, "version"},
            {InstallPathRole, "installPath"},
            {UserDataPathRole, "userDataPath"},
            {ManualLocationRole, "manualLocation"}};
}

QString GamesModel::currentName() const {
    const GameRowData* row = find(m_current);
    return row ? row->name : QString();
}

bool GamesModel::currentInstalled() const {
    const GameRowData* row = find(m_current);
    return row && row->installed;
}

bool GamesModel::currentSupported() const {
    const GameRowData* row = find(m_current);
    return row && row->supported;
}

void GamesModel::select(const QString& key) {
    m_backend->selectGame(key);
}

void GamesModel::rescan() {
    m_backend->rescan(true);
}

bool GamesModel::locate(const QString& key, const QUrl& installDirUrl, const QUrl& userDataDirUrl) {
    return m_backend->locateGame(key, installDirUrl, userDataDirUrl);
}

void GamesModel::clearManualLocation(const QString& key) {
    m_backend->clearManualLocation(key);
}

bool GamesModel::importCurrentConfiguration() {
    return m_backend->importCurrentConfiguration();
}

void GamesModel::setRows(std::vector<GameRowData> rows) {
    const bool sameKeys =
        rows.size() == m_rows.size() &&
        std::equal(rows.begin(), rows.end(), m_rows.begin(),
                   [](const GameRowData& a, const GameRowData& b) { return a.key == b.key; });
    if (!sameKeys) {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
        emit countChanged();
    } else {
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (sameRow(rows[i], m_rows[i])) {
                m_rows[i] = std::move(rows[i]); // mod counts and scan errors
                continue;
            }
            m_rows[i] = std::move(rows[i]);
            const QModelIndex changed = index(static_cast<int>(i));
            emit dataChanged(changed, changed);
        }
    }
    emit currentChanged();
}

void GamesModel::updateRow(const GameRowData& row) {
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].key != row.key)
            continue;
        m_rows[i] = row;
        const QModelIndex changed = index(static_cast<int>(i));
        emit dataChanged(changed, changed);
        if (row.key == m_current)
            emit currentChanged();
        return;
    }
}

void GamesModel::setCurrent(const QString& key) {
    if (m_current == key)
        return;
    m_current = key;
    emit currentChanged();
}

void GamesModel::setScanning(bool scanning) {
    if (m_scanning == scanning)
        return;
    m_scanning = scanning;
    emit scanningChanged();
}

const GameRowData* GamesModel::find(const QString& key) const {
    for (const GameRowData& row : m_rows) {
        if (row.key == key)
            return &row;
    }
    return nullptr;
}

} // namespace pmm::app
