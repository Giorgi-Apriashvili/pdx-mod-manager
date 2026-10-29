#include "app/gui/mod_list_model.hpp"

#include "app/core/qt_util.hpp"
#include "app/core/service_runner.hpp"
#include "app/gui/backend.hpp"
#include "app/gui/mod_filter_model.hpp"
#include "app/gui/notification_center.hpp"
#include "core/playset_ops.hpp"

#include <QHash>

#include <algorithm>
#include <climits>

namespace pmm::app {
namespace {

constexpr int kMaxFeedbackLines = 6;

bool sameDependencies(const std::vector<DependencyData>& a, const std::vector<DependencyData>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](const DependencyData& x, const DependencyData& y) {
                          return x.name == y.name && x.state == y.state && x.text == y.text;
                      });
}

bool sameData(const ModRowData& a, const ModRowData& b) {
    return a.entry == b.entry && a.key == b.key && a.name == b.name && a.status == b.status &&
           a.statusText == b.statusText && a.warnings == b.warnings && a.progress == b.progress &&
           a.blocking == b.blocking && a.version == b.version &&
           a.supportedVersion == b.supportedVersion && a.tags == b.tags &&
           a.problems == b.problems && a.workshopUrl == b.workshopUrl &&
           a.contentPath == b.contentPath && a.canOpenFolder == b.canOpenFolder &&
           sameDependencies(a.dependencies, b.dependencies);
}

bool isMissingStatus(const QString& status) {
    return status == QLatin1String("missing") || status == QLatin1String("pending") ||
           status == QLatin1String("downloading") || status == QLatin1String("failed") ||
           status == QLatin1String("unavailable") || status == QLatin1String("needs_user_action");
}

QString formatIssues(const std::vector<DependencyIssue>& issues) {
    QStringList lines;
    for (const DependencyIssue& issue : issues) {
        if (lines.size() == kMaxFeedbackLines) {
            lines.push_back(
                QStringLiteral("… and %1 more.").arg(issues.size() - kMaxFeedbackLines));
            break;
        }
        lines.push_back(qs(issue.message));
    }
    return lines.join(QLatin1Char('\n'));
}

// First entry of a block lands at `target` when inserted before the returned
// gap (core moveBlock() semantics: gaps index the original list).
std::size_t gapForTarget(const std::vector<std::size_t>& sorted, std::size_t size,
                         std::size_t target) {
    std::size_t selectedBefore = 0;
    for (std::size_t gap = 0; gap <= size; ++gap) {
        while (selectedBefore < sorted.size() && sorted[selectedBefore] < gap)
            ++selectedBefore;
        if (gap - selectedBefore == target)
            return gap;
    }
    return size;
}

} // namespace

ModListModel::ModListModel(Backend* backend, QObject* parent)
    : QAbstractListModel(parent), m_backend(backend) {}

int ModListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant ModListModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const ModRowData& row = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case PositionRole:
        return index.row() + 1;
    case EnabledRole:
        return row.entry.enabled;
    case Qt::DisplayRole:
    case NameRole:
        return row.name;
    case SourceRole:
        return row.source;
    case SourceLabelRole:
        return row.sourceLabel;
    case ProviderIdRole:
        return row.providerId;
    case StatusRole:
        return row.status;
    case StatusTextRole:
        return row.statusText;
    case WarningsRole:
        return row.warnings;
    case HasWarningsRole:
        return !row.warnings.isEmpty();
    case ProgressRole:
        return row.progress;
    case BlockingRole:
        return row.blocking;
    case IdentityKeyRole:
        return row.key;
    case VersionRole:
        return row.version;
    default:
        return {};
    }
}

QHash<int, QByteArray> ModListModel::roleNames() const {
    return {{PositionRole, "position"},
            {EnabledRole, "enabled"},
            {NameRole, "name"},
            {SourceRole, "source"},
            {SourceLabelRole, "sourceLabel"},
            {ProviderIdRole, "providerId"},
            {StatusRole, "status"},
            {StatusTextRole, "statusText"},
            {WarningsRole, "warnings"},
            {HasWarningsRole, "hasWarnings"},
            {ProgressRole, "progress"},
            {BlockingRole, "blocking"},
            {IdentityKeyRole, "identityKey"},
            {VersionRole, "version"}};
}

QVariantMap ModListModel::get(int row) const {
    QVariantMap out;
    if (row < 0 || row >= count())
        return out;
    const QModelIndex at = index(row);
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        out.insert(QString::fromLatin1(it.value()), data(at, it.key()));
    return out;
}

// --- Edits ----------------------------------------------------------------------

void ModListModel::setEnabled(int row, bool on) {
    setEnabledRows({row}, on);
}

QString ModListModel::setEnabledRows(const QList<int>& rows, bool on) {
    const std::vector<std::size_t> indices = validRows(rows);
    if (indices.empty())
        return {};
    std::vector<char> changed(m_rows.size(), 0);
    bool any = false;
    for (std::size_t i : indices) {
        ModRowData& row = m_rows[i];
        if (row.entry.enabled == on)
            continue;
        row.entry.enabled = on;
        row.blocking = on && row.status != QLatin1String("ready");
        changed[i] = 1;
        any = true;
    }
    if (!any)
        return {};
    emitChangedRuns(changed);
    recount();
    runEdit(
        on ? tr("Could not enable the mods") : tr("Could not disable the mods"),
        [indices, on](AppRuntime& runtime, std::int64_t playsetId) -> EditOutcome {
            auto issues = runtime.playsets().setEnabled(playsetId, indices, on);
            if (!issues)
                return {issues.error(), {}};
            return {std::nullopt, on ? formatIssues(issues.value()) : QString()};
        },
        true);
    return {};
}

void ModListModel::moveRow(int from, int to) {
    if (from == to || from < 0 || to < 0 || from >= count() || to >= count())
        return;
    if (refuseReorder())
        return;
    std::vector<PlaysetEntry> ordered = entries();
    if (!moveEntry(ordered, static_cast<std::size_t>(from), static_cast<std::size_t>(to)))
        return;
    applyOrder(ordered);
    const ViewFilter filter = viewFilter();
    runEdit(
        tr("Could not move the mod"),
        [from, to, filter](AppRuntime& runtime, std::int64_t playsetId) -> EditOutcome {
            Status moved = runtime.playsets().move(playsetId, static_cast<std::size_t>(from),
                                                   static_cast<std::size_t>(to), filter);
            if (!moved)
                return {moved.error(), {}};
            return {};
        },
        false);
}

void ModListModel::moveUp(int row) {
    moveRows({row}, -1);
}

void ModListModel::moveDown(int row) {
    moveRows({row}, 1);
}

void ModListModel::moveRows(const QList<int>& rows, int delta) {
    const std::vector<std::size_t> indices = validRows(rows);
    if (indices.empty() || delta == 0)
        return;
    if (refuseReorder())
        return;
    std::vector<PlaysetEntry> ordered = entries();
    const std::vector<PlaysetEntry> before = ordered;
    const ViewFilter filter = viewFilter();
    EditOp op;
    if (delta == -1 || delta == 1) {
        auto moved =
            delta < 0 ? moveSelectionUp(ordered, indices) : moveSelectionDown(ordered, indices);
        if (!moved)
            return;
        op = [indices, delta, filter](AppRuntime& runtime, std::int64_t playsetId) -> EditOutcome {
            auto result = delta < 0 ? runtime.playsets().moveUp(playsetId, indices, filter)
                                    : runtime.playsets().moveDown(playsetId, indices, filter);
            if (!result)
                return {result.error(), {}};
            return {};
        };
    } else {
        const std::size_t size = m_rows.size();
        const std::size_t maxTarget = size - indices.size();
        const long long wanted = static_cast<long long>(indices.front()) + delta;
        const std::size_t target = static_cast<std::size_t>(
            std::clamp<long long>(wanted, 0, static_cast<long long>(maxTarget)));
        const std::size_t gap = gapForTarget(indices, size, target);
        if (!moveBlock(ordered, indices, gap))
            return;
        op = [indices, gap, filter](AppRuntime& runtime, std::int64_t playsetId) -> EditOutcome {
            auto result = runtime.playsets().moveBlock(playsetId, indices, gap, filter);
            if (!result)
                return {result.error(), {}};
            return {};
        };
    }
    if (ordered == before)
        return; // already at the edge: no undo step
    applyOrder(ordered);
    runEdit(tr("Could not move the mods"), std::move(op), false);
}

void ModListModel::removeRows(const QList<int>& rows) {
    const std::vector<std::size_t> indices = validRows(rows);
    if (indices.empty())
        return;
    for (std::size_t k = indices.size(); k > 0;) {
        const std::size_t last = indices[--k];
        std::size_t first = last;
        while (k > 0 && indices[k - 1] == first - 1)
            first = indices[--k];
        beginRemoveRows({}, static_cast<int>(first), static_cast<int>(last));
        m_rows.erase(m_rows.begin() + static_cast<std::ptrdiff_t>(first),
                     m_rows.begin() + static_cast<std::ptrdiff_t>(last) + 1);
        endRemoveRows();
        if (static_cast<int>(first) < count()) {
            std::vector<char> shifted(m_rows.size(), 0);
            std::fill(shifted.begin() + static_cast<std::ptrdiff_t>(first), shifted.end(), char{1});
            emitChangedRuns(shifted);
        }
    }
    emit countChanged();
    recount();
    const int removed = static_cast<int>(indices.size());
    runEdit(
        tr("Could not remove the mods"),
        [indices](AppRuntime& runtime, std::int64_t playsetId) -> EditOutcome {
            Status done = runtime.playsets().removeEntries(playsetId, indices);
            if (!done)
                return {done.error(), {}};
            return {};
        },
        true);
    m_backend->notifications()->info(
        tr("Removed from playset"),
        tr("%1 removed from the playset. Removed mods stay installed; press Ctrl+Z to undo.")
            .arg(countText(removed, "mod", "mods")));
}

void ModListModel::addMods(const QStringList& identityKeys) {
    if (m_playsetId < 0 || identityKeys.isEmpty())
        return;
    std::vector<ModIdentity> identities;
    for (const QString& key : identityKeys) {
        const qsizetype colon = key.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        identities.push_back(
            ModIdentity{utf8(m_game), utf8(key.left(colon)), utf8(key.mid(colon + 1))});
    }
    if (identities.empty())
        return;
    runEdit(
        tr("Could not add the mods"),
        [identities](AppRuntime& runtime, std::int64_t playsetId) -> EditOutcome {
            Status added = runtime.playsets().addMods(playsetId, identities);
            if (!added)
                return {added.error(), {}};
            return {};
        },
        true);
}

void ModListModel::undo() {
    if (!m_undo.canUndo)
        return;
    runEdit(
        tr("Could not undo"),
        [](AppRuntime& runtime, std::int64_t playsetId) -> EditOutcome {
            Status done = runtime.playsets().undo(playsetId);
            if (!done)
                return {done.error(), {}};
            return {};
        },
        true, false);
}

void ModListModel::redo() {
    if (!m_undo.canRedo)
        return;
    runEdit(
        tr("Could not redo"),
        [](AppRuntime& runtime, std::int64_t playsetId) -> EditOutcome {
            Status done = runtime.playsets().redo(playsetId);
            if (!done)
                return {done.error(), {}};
            return {};
        },
        true, false);
}

void ModListModel::runEdit(const QString& failureTitle, EditOp op, bool affectsLaunch,
                           bool undoable) {
    if (m_playsetId < 0)
        return;
    struct Outcome {
        EditOutcome edit;
        PlaysetStateData state;
        std::optional<LaunchData> launch;
    };
    const std::int64_t playsetId = m_playsetId;
    const QString game = m_game;
    const QueryOptions options = m_backend->queryOptions();
    if (++m_pendingEdits == 1)
        emit busyChanged();
    if (undoable && !m_undo.canUndo) {
        // Every edit can be undone; the saved state brings the exact label.
        m_undo.canUndo = true;
        emit undoChanged();
    }
    m_backend->runner().call(
        this,
        [op = std::move(op), playsetId, game, options, affectsLaunch](AppRuntime& runtime) {
            Outcome outcome;
            outcome.edit = op(runtime, playsetId);
            outcome.state = queries::playsetState(runtime, playsetId);
            if (affectsLaunch)
                outcome.launch = queries::launchData(runtime, game, playsetId, options);
            return outcome;
        },
        [this, playsetId, failureTitle](Outcome outcome) {
            finishEdit(playsetId, failureTitle, std::move(outcome.edit), std::move(outcome.state),
                       std::move(outcome.launch));
        });
}

void ModListModel::finishEdit(std::int64_t playsetId, const QString& failureTitle,
                              EditOutcome outcome, PlaysetStateData state,
                              std::optional<LaunchData> launch) {
    if (--m_pendingEdits == 0)
        emit busyChanged();
    if (playsetId != m_playsetId)
        return; // another playset is shown now
    if (outcome.error) {
        const bool refused = outcome.error->code == ErrorCode::AlreadyExists ||
                             outcome.error->code == ErrorCode::InvalidArgument ||
                             outcome.error->code == ErrorCode::NotFound;
        m_backend->notifications()->post(refused ? QStringLiteral("warning")
                                                 : QStringLiteral("error"),
                                         failureTitle, qs(outcome.error->message));
    }
    if (!outcome.feedback.isEmpty())
        emit dependencyFeedback(outcome.feedback);
    // Results arrive in order: the newest launch verdict is the relevant one
    // (edits without one, i.e. moves, do not change readiness).
    if (launch)
        m_latestLaunch = std::move(launch);
    if (m_pendingEdits > 0 || state.playsetId != m_playsetId)
        return; // a later result carries the newer state
    m_name = state.name;
    reconcile(std::move(state.rows));
    setUndo(state.undo);
    m_backend->playsetEdited(std::exchange(m_latestLaunch, std::nullopt));
    emit edited();
}

bool ModListModel::refuseReorder() {
    if (m_playsetId < 0)
        return true;
    if (m_filter && !m_filter->reorderAllowed()) {
        m_backend->notifications()->warning(tr("Reordering is disabled"),
                                            m_filter->reorderBlockedReason());
        return true;
    }
    return false;
}

ViewFilter ModListModel::viewFilter() const {
    if (!m_filter)
        return {};
    return queries::toViewFilter(m_filter->search(), m_filter->statusFilter(),
                                 m_filter->sortMode());
}

std::vector<std::size_t> ModListModel::validRows(const QList<int>& rows) const {
    std::vector<std::size_t> out;
    if (m_playsetId < 0)
        return out;
    for (int row : rows) {
        if (row >= 0 && row < count())
            out.push_back(static_cast<std::size_t>(row));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<PlaysetEntry> ModListModel::entries() const {
    std::vector<PlaysetEntry> out;
    out.reserve(m_rows.size());
    for (const ModRowData& row : m_rows)
        out.push_back(row.entry);
    return out;
}

// --- Model updates -------------------------------------------------------------

void ModListModel::applyOrder(const std::vector<PlaysetEntry>& ordered) {
    QHash<QString, std::size_t> current;
    current.reserve(static_cast<qsizetype>(m_rows.size()));
    for (std::size_t i = 0; i < m_rows.size(); ++i)
        current.insert(m_rows[i].key, i);
    std::vector<ModRowData> rows;
    rows.reserve(m_rows.size());
    std::vector<char> changed(m_rows.size(), 0);
    for (const PlaysetEntry& entry : ordered) {
        const auto it = current.constFind(identityKey(entry.identity.source, entry.identity.id));
        if (it == current.cend())
            return; // not the same identities; the worker's state will correct it
        if (it.value() != rows.size())
            changed[rows.size()] = 1;
        rows.push_back(m_rows[it.value()]);
    }
    if (rows.size() != m_rows.size())
        return;
    m_rows = std::move(rows);
    emitChangedRuns(changed);
}

void ModListModel::reconcile(std::vector<ModRowData> target) {
    const std::size_t before = m_rows.size();
    QHash<QString, std::size_t> targetIndex;
    targetIndex.reserve(static_cast<qsizetype>(target.size()));
    for (std::size_t i = 0; i < target.size(); ++i)
        targetIndex.insert(target[i].key, i);
    std::size_t common = 0;
    for (const ModRowData& row : m_rows)
        common += targetIndex.contains(row.key) ? 1 : 0;
    const std::size_t structural = (before - common) + (target.size() - common);
    if (before == 0 || target.empty() ||
        static_cast<std::size_t>(targetIndex.size()) != target.size() ||
        structural > std::max<std::size_t>(64, before / 4)) {
        beginResetModel();
        m_rows = std::move(target);
        endResetModel();
        if (before != m_rows.size())
            emit countChanged();
        recount();
        return;
    }

    int firstShift = INT_MAX;
    // Removed entries, from the end, in contiguous runs.
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0;) {
        if (targetIndex.contains(m_rows[static_cast<std::size_t>(i)].key)) {
            --i;
            continue;
        }
        const int last = i;
        while (i >= 0 && !targetIndex.contains(m_rows[static_cast<std::size_t>(i)].key))
            --i;
        const int first = i + 1;
        beginRemoveRows({}, first, last);
        m_rows.erase(m_rows.begin() + first, m_rows.begin() + last + 1);
        endRemoveRows();
        firstShift = std::min(firstShift, first);
    }

    // Remaining entries in the target's relative order.
    QSet<QString> present;
    QHash<QString, std::size_t> currentIndex;
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        present.insert(m_rows[i].key);
        currentIndex.insert(m_rows[i].key, i);
    }
    std::vector<ModRowData> ordered;
    ordered.reserve(m_rows.size());
    std::vector<char> changed;
    for (const ModRowData& wanted : target) {
        const auto it = currentIndex.constFind(wanted.key);
        if (it == currentIndex.cend())
            continue;
        changed.push_back(it.value() != ordered.size() ? 1 : 0);
        ordered.push_back(std::move(m_rows[it.value()]));
    }
    m_rows = std::move(ordered);

    // Added entries.
    for (std::size_t i = 0; i < target.size();) {
        if (present.contains(target[i].key)) {
            ++i;
            continue;
        }
        const std::size_t first = i;
        while (i < target.size() && !present.contains(target[i].key))
            ++i;
        beginInsertRows({}, static_cast<int>(first), static_cast<int>(i - 1));
        m_rows.insert(m_rows.begin() + static_cast<std::ptrdiff_t>(first),
                      target.begin() + static_cast<std::ptrdiff_t>(first),
                      target.begin() + static_cast<std::ptrdiff_t>(i));
        changed.insert(changed.begin() + static_cast<std::ptrdiff_t>(first), i - first, char{0});
        endInsertRows();
        firstShift = std::min(firstShift, static_cast<int>(first));
    }

    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        if (!sameData(m_rows[i], target[i])) {
            m_rows[i] = std::move(target[i]);
            changed[i] = 1;
        } else if (firstShift != INT_MAX && static_cast<int>(i) >= firstShift) {
            changed[i] = 1; // position changed
        }
    }
    if (before != m_rows.size())
        emit countChanged();
    emitChangedRuns(changed);
    recount();
}

void ModListModel::emitChangedRuns(const std::vector<char>& changed) {
    const std::size_t size = std::min(changed.size(), m_rows.size());
    for (std::size_t i = 0; i < size;) {
        if (!changed[i]) {
            ++i;
            continue;
        }
        const std::size_t first = i;
        while (i < size && changed[i])
            ++i;
        emit dataChanged(index(static_cast<int>(first)), index(static_cast<int>(i - 1)));
    }
}

void ModListModel::resetTo(PlaysetStateData state) {
    const std::size_t before = m_rows.size();
    m_latestLaunch.reset();
    beginResetModel();
    m_playsetId = state.playsetId;
    m_game = state.game;
    m_name = state.name;
    m_rows = std::move(state.rows);
    endResetModel();
    if (before != m_rows.size())
        emit countChanged();
    recount();
    setUndo(state.undo);
}

void ModListModel::applyState(PlaysetStateData state) {
    if (state.playsetId != m_playsetId) {
        resetTo(std::move(state));
        return;
    }
    if (m_pendingEdits > 0)
        return;
    m_name = state.name;
    reconcile(std::move(state.rows));
    setUndo(state.undo);
}

const ModRowData* ModListModel::row(int index) const {
    if (index < 0 || index >= count())
        return nullptr;
    return &m_rows[static_cast<std::size_t>(index)];
}

QSet<QString> ModListModel::keys() const {
    QSet<QString> out;
    out.reserve(static_cast<qsizetype>(m_rows.size()));
    for (const ModRowData& row : m_rows)
        out.insert(row.key);
    return out;
}

void ModListModel::recount() {
    CountsData counts;
    counts.total = count();
    for (const ModRowData& row : m_rows) {
        counts.enabled += row.entry.enabled ? 1 : 0;
        counts.ready += row.status == QLatin1String("ready") ? 1 : 0;
        counts.missing += isMissingStatus(row.status) ? 1 : 0;
        counts.warning += row.status == QLatin1String("broken") || !row.warnings.isEmpty() ? 1 : 0;
        counts.blocking += row.blocking ? 1 : 0;
    }
    setCounts(counts);
}

void ModListModel::setCounts(const CountsData& counts) {
    if (counts.total == m_counts.total && counts.enabled == m_counts.enabled &&
        counts.ready == m_counts.ready && counts.missing == m_counts.missing &&
        counts.warning == m_counts.warning && counts.blocking == m_counts.blocking)
        return;
    m_counts = counts;
    emit countsChanged();
}

void ModListModel::setUndo(const UndoData& undo) {
    if (undo.canUndo == m_undo.canUndo && undo.canRedo == m_undo.canRedo &&
        undo.undoText == m_undo.undoText && undo.redoText == m_undo.redoText)
        return;
    m_undo = undo;
    emit undoChanged();
}

} // namespace pmm::app
