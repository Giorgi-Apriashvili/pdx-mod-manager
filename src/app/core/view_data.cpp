#include "app/core/view_data.hpp"

#include "app/core/qt_util.hpp"
#include "core/dependencies.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "platform/process.hpp"
#include "providers/fallback.hpp"
#include "services/playset_view.hpp"

#include <QDateTime>
#include <QLocale>

#include <algorithm>
#include <map>
#include <set>
#include <system_error>

namespace pmm::app::queries {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kNoPlaysetBlocker =
    "No active playset. Create a playset, import a share code or use the game's current "
    "configuration.";

QString sourceLabel(const std::string& source) {
    if (source == source::SteamWorkshop)
        return QStringLiteral("Workshop");
    if (source == source::Local)
        return QStringLiteral("Local");
    return qs(source);
}

QString trimmedQ(const std::string& text) {
    return qs(text).trimmed();
}

QString dependencyState(DependencyIssueKind kind) {
    switch (kind) {
    case DependencyIssueKind::NotInstalled:
        return QStringLiteral("missing");
    case DependencyIssueKind::NotInPlayset:
        return QStringLiteral("not_in_playset");
    case DependencyIssueKind::Disabled:
        return QStringLiteral("disabled");
    case DependencyIssueKind::OrderedAfter:
        return QStringLiteral("order");
    }
    return QStringLiteral("missing");
}

std::vector<DependencyData> dependenciesOf(const services::PlaysetRow& row,
                                           const std::vector<const DependencyIssue*>& issues) {
    std::vector<DependencyData> out;
    if (!row.installed)
        return out;
    for (const std::string& raw : row.installed->descriptor.dependencies) {
        const QString name = trimmedQ(raw);
        if (name.isEmpty())
            continue;
        DependencyData dependency;
        dependency.name = name;
        const auto issue =
            std::find_if(issues.begin(), issues.end(),
                         [&](const DependencyIssue* i) { return qs(i->dependencyName) == name; });
        if (issue != issues.end()) {
            dependency.state = dependencyState((*issue)->kind);
            dependency.text = qs((*issue)->message);
        } else if (row.entry.enabled) {
            dependency.state = QStringLiteral("ok");
            dependency.text = QStringLiteral("Enabled in this playset and loads earlier");
        } else {
            dependency.state = QStringLiteral("unchecked");
            dependency.text = QStringLiteral("Checked when this mod is enabled");
        }
        out.push_back(std::move(dependency));
    }
    return out;
}

bool showsDetailAsProblem(EntryStatus status) {
    switch (status) {
    case EntryStatus::Missing:
    case EntryStatus::Failed:
    case EntryStatus::Unavailable:
    case EntryStatus::NeedsUserAction:
    case EntryStatus::Broken:
        return true;
    case EntryStatus::Ready:
    case EntryStatus::Pending:
    case EntryStatus::Downloading:
        return false;
    }
    return false;
}

ModRowData modRow(const services::PlaysetRow& row,
                  const std::vector<const DependencyIssue*>& issues) {
    ModRowData data;
    data.entry = row.entry;
    const ModIdentity& identity = row.entry.identity;
    data.key = identityKey(identity.source, identity.id);
    data.name = qs(row.displayName);
    data.source = qs(identity.source);
    data.sourceLabel = sourceLabel(identity.source);
    data.providerId = qs(identity.id);
    data.status = QString::fromLatin1(toString(row.readiness.status));
    data.statusText = statusTextFor(row.readiness, row.job);
    data.warnings = qlist(row.readiness.warnings);
    data.progress =
        row.readiness.status == EntryStatus::Downloading ? row.readiness.progress : -1.0;
    data.blocking = row.blocking;
    if (row.installed) {
        const InstalledMod& mod = *row.installed;
        data.version = trimmedQ(mod.descriptor.version);
        data.supportedVersion = trimmedQ(mod.descriptor.supportedVersion);
        for (const std::string& tag : mod.descriptor.tags) {
            if (const QString t = trimmedQ(tag); !t.isEmpty())
                data.tags.push_back(t);
        }
        for (const std::string& problem : mod.problems)
            data.problems.push_back(qs(problem));
        data.contentPath = qpath(mod.contentPath);
        // Existence is checked when the folder is opened, not for every row of
        // every refresh.
        data.canOpenFolder = !mod.contentPath.empty();
    } else if (!row.entry.versionHint.empty()) {
        data.version = trimmedQ(row.entry.versionHint);
    }
    if (showsDetailAsProblem(row.readiness.status) && !row.readiness.detail.empty()) {
        const QString detail = qs(row.readiness.detail);
        if (!data.problems.contains(detail))
            data.problems.push_back(detail);
    }
    data.dependencies = dependenciesOf(row, issues);
    if (identity.source == source::SteamWorkshop) {
        if (auto url = providers::workshopWebItemUrl(identity.id))
            data.workshopUrl = qs(url.value());
    }
    return data;
}

// Blockers that do not depend on a playset: installation and executable
// missing, the game or the Paradox launcher running. Reads only.
void installationBlockers(AppRuntime& runtime, const GameKey& key, LaunchData& data) {
    const std::string gameName = services::gameDisplayName(key);
    auto installation = runtime.games().installation(key);
    if (!installation || !installation.value()) {
        data.blockers.push_back(qs(gameName + " has not been found. Rescan, or locate the game "
                                              "folder."));
    } else {
        const GameInstallation& install = *installation.value();
        std::error_code ec;
        if (install.executablePath.empty() || !fs::is_regular_file(install.executablePath, ec))
            data.blockers.push_back(qs("The " + gameName + " executable was not found."));
        const games::GameAdapter* adapter = runtime.context().adapterFor(key);
        if (adapter && runtime.context().listProcesses) {
            if (auto processes = runtime.context().listProcesses()) {
                const games::ProcessNames names = adapter->processNames(install);
                std::set<std::string> reported;
                for (const platform::RunningProcess& process : processes.value()) {
                    for (const std::string& name : names.game) {
                        if (platform::executableNameMatches(process.executableName, name) &&
                            reported.insert(name).second)
                            data.blockers.push_back(
                                qs(gameName + " is already running. Close it before launching."));
                    }
                    for (const std::string& name : names.conflictingWriters) {
                        if (platform::executableNameMatches(process.executableName, name) &&
                            reported.insert(name).second)
                            data.blockers.push_back(
                                qs("The Paradox launcher is running (" + process.executableName +
                                   "). Close it before launching, so it does not overwrite the mod "
                                   "configuration."));
                    }
                }
            }
        }
    }
}

} // namespace

GameRowData gameRow(const services::GameState& state) {
    GameRowData row;
    row.key = qs(state.key);
    row.name = qs(state.displayName);
    row.supported = state.supported;
    row.installed = state.installation.has_value();
    row.modCount = static_cast<int>(state.inventory.size());
    row.loadOrderNote = qs(state.loadOrderNote);
    if (state.scanError)
        row.scanError = qs(state.scanError->message);
    if (!state.supported) {
        row.statusText = QStringLiteral("Not supported yet");
    } else if (state.installation) {
        const GameInstallation& install = *state.installation;
        row.version = qs(install.version);
        row.manualLocation = install.manuallyConfigured;
        row.installPath = qpath(install.installPath);
        row.userDataPath = qpath(install.userDataPath);
        row.statusText = QStringLiteral("Installed");
        if (!install.version.empty())
            row.statusText += QStringLiteral(" · ") + row.version;
        if (install.manuallyConfigured)
            row.statusText += QStringLiteral(" · manual location");
    } else {
        row.statusText = state.scanError ? QStringLiteral("Not found · check location")
                                         : QStringLiteral("Not found");
    }
    return row;
}

std::vector<GameRowData> gameRows(const std::vector<services::GameState>& states) {
    std::vector<GameRowData> rows;
    rows.reserve(states.size());
    for (const services::GameState& state : states)
        rows.push_back(gameRow(state));
    return rows;
}

QString relativeTime(std::int64_t unixMs) {
    if (unixMs <= 0)
        return {};
    const QDateTime when = QDateTime::fromMSecsSinceEpoch(unixMs).toLocalTime();
    const QDate today = QDate::currentDate();
    const QString time = when.time().toString(QStringLiteral("HH:mm"));
    if (when.date() == today)
        return QStringLiteral("Today %1").arg(time);
    if (when.date() == today.addDays(-1))
        return QStringLiteral("Yesterday %1").arg(time);
    return QLocale::c().toString(when.date(), QStringLiteral("d MMM yyyy"));
}

QString historyReasonText(const std::string& reason) {
    if (reason == persistence::history_reason::Deleted)
        return QStringLiteral("Deleted");
    if (reason == persistence::history_reason::ImportReplace)
        return QStringLiteral("Replaced by import");
    if (reason == persistence::history_reason::BeforeRestore)
        return QStringLiteral("Before a restore");
    if (reason == persistence::history_reason::BulkEdit)
        return QStringLiteral("Before a bulk edit");
    if (reason == services::kHistoryReasonEntriesRemoved)
        return QStringLiteral("Mods removed");
    return qs(reason);
}

QString statusTextFor(const EntryReadiness& readiness, const std::optional<AcquisitionJob>& job) {
    switch (readiness.status) {
    case EntryStatus::Ready:
        return QStringLiteral("Ready");
    case EntryStatus::Missing:
        return QStringLiteral("Missing");
    case EntryStatus::Pending:
        if (job) {
            switch (job->state) {
            case AcquisitionState::Subscribing:
                return QStringLiteral("Subscribing");
            case AcquisitionState::Subscribed:
                return QStringLiteral("Subscribed");
            case AcquisitionState::DownloadRequested:
                return QStringLiteral("Waiting for Steam");
            case AcquisitionState::Installed:
                return QStringLiteral("Verifying");
            default:
                break;
            }
        }
        return QStringLiteral("Queued");
    case EntryStatus::Downloading:
        if (readiness.progress >= 0.0)
            return QStringLiteral("Downloading %1 %")
                .arg(static_cast<int>(std::clamp(readiness.progress, 0.0, 1.0) * 100.0));
        return QStringLiteral("Downloading");
    case EntryStatus::Failed:
        return QStringLiteral("Failed");
    case EntryStatus::Unavailable:
        return QStringLiteral("Unavailable");
    case EntryStatus::NeedsUserAction:
        return QStringLiteral("Action needed");
    case EntryStatus::Broken:
        return QStringLiteral("Broken");
    }
    return QStringLiteral("Unknown");
}

ViewFilter toViewFilter(const QString& search, const QString& statusFilter,
                        const QString& sortMode) {
    ViewFilter filter;
    filter.search = utf8(search.trimmed());
    if (statusFilter == QLatin1String("enabled"))
        filter.status = StatusFilter::Enabled;
    else if (statusFilter == QLatin1String("disabled"))
        filter.status = StatusFilter::Disabled;
    else if (statusFilter == QLatin1String("missing"))
        filter.status = StatusFilter::Missing;
    else if (statusFilter == QLatin1String("warning"))
        filter.status = StatusFilter::Warning;
    filter.browseSorted = !sortMode.isEmpty() && sortMode != QLatin1String("order");
    return filter;
}

PlaysetListData playsetList(AppRuntime& runtime, const QString& game) {
    PlaysetListData data;
    const GameKey key = utf8(game);
    services::PlaysetService& playsets = runtime.playsets();
    if (auto list = playsets.listPlaysets(key)) {
        for (const PlaysetSummary& summary : list.value()) {
            PlaysetSummaryData row;
            row.id = summary.id;
            row.name = qs(summary.name);
            row.entryCount = summary.entryCount;
            row.enabledCount = summary.enabledCount;
            row.updatedText = relativeTime(summary.updatedAt);
            data.playsets.push_back(std::move(row));
        }
    }
    if (auto active = playsets.activePlaysetId(key); active && active.value())
        data.activeId = *active.value();
    if (auto deleted = playsets.lastDeleted(key); deleted && deleted.value())
        data.lastDeletedHistoryId = deleted.value()->id;
    if (auto history = playsets.history(key)) {
        for (const persistence::PlaysetHistoryEntry& entry : history.value()) {
            HistoryData row;
            row.historyId = entry.id;
            row.reason = historyReasonText(entry.reason);
            row.playsetName = qs(entry.snapshot.name);
            row.createdText = relativeTime(entry.createdAt);
            data.history.push_back(std::move(row));
        }
    }
    return data;
}

PlaysetStateData playsetState(AppRuntime& runtime, std::int64_t playsetId, QString* error) {
    PlaysetStateData data;
    if (playsetId < 0)
        return data;
    auto playset = runtime.playsets().playset(playsetId);
    if (!playset) {
        if (error)
            *error = qs(playset.error().message);
        return data;
    }
    const GameKey game = playset.value().game;
    const services::AcquisitionCoordinator* coordinator = runtime.acquisition();
    const std::vector<AcquisitionJob> live =
        coordinator ? coordinator->liveJobs(game) : std::vector<AcquisitionJob>{};
    auto view = services::loadPlaysetView(runtime.context(), playsetId, live);
    if (!view) {
        if (error)
            *error = qs(view.error().message);
        return data;
    }
    const services::PlaysetView& v = view.value();
    data.playsetId = v.playsetId;
    data.game = qs(v.game);
    data.name = qs(v.name);
    std::map<std::size_t, std::vector<const DependencyIssue*>> issues;
    for (const DependencyIssue& issue : v.dependencyIssues)
        issues[issue.entryIndex].push_back(&issue);
    static const std::vector<const DependencyIssue*> kNone;
    data.rows.reserve(v.rows.size());
    for (const services::PlaysetRow& row : v.rows) {
        const auto it = issues.find(row.index);
        data.rows.push_back(modRow(row, it != issues.end() ? it->second : kNone));
    }
    data.counts.total = static_cast<int>(v.counts.total);
    data.counts.enabled = static_cast<int>(v.counts.enabled);
    data.counts.ready = static_cast<int>(v.counts.ready);
    data.counts.missing = static_cast<int>(v.counts.missing);
    data.counts.warning = static_cast<int>(v.counts.warning);
    data.counts.blocking = static_cast<int>(v.counts.blocking);
    const services::UndoInfo undo = runtime.playsets().undoInfo(playsetId);
    data.undo.canUndo = undo.canUndo;
    data.undo.canRedo = undo.canRedo;
    data.undo.undoText = qs(undo.undoText);
    data.undo.redoText = qs(undo.redoText);
    return data;
}

LaunchData launchData(AppRuntime& runtime, const QString& game, std::int64_t playsetId,
                      const QueryOptions& options) {
    const GameKey key = utf8(game);
    LaunchData data;
    if (playsetId < 0) {
        installationBlockers(runtime, key, data);
        data.blockers.push_back(qs(kNoPlaysetBlocker));
        if (auto interrupted = runtime.launcher().interruptedTransactions())
            data.interrupted = !interrupted.value().empty();
    } else {
        // Read-only mode runs the same check without its write probes in the
        // game's user-data folder.
        services::LaunchCheckOptions checkOptions;
        checkOptions.probeWriteAccess = !options.readOnly;
        const services::LaunchCheck check = runtime.launcher().check(key, playsetId, checkOptions);
        data.canPlay = check.canLaunch;
        data.blockers = qlist(check.blockers);
        data.warnings = qlist(check.warnings);
        data.interrupted = check.interruptedTransaction;
    }
    if (options.readOnly) {
        // Launching and restoring a backup would write game configuration.
        data.canPlay = false;
        data.blockers.push_back(QStringLiteral(
            "Read-only verification mode: launching and game configuration writes are disabled."));
        return data;
    }
    if (auto restorable = runtime.launcher().lastRestorableTransaction(key))
        data.canRestoreBackup = restorable.value().has_value();
    return data;
}

std::vector<InventoryRowData> inventoryRows(AppRuntime& runtime, const QString& game) {
    std::vector<InventoryRowData> rows;
    auto inventory = runtime.games().cachedInventory(utf8(game));
    if (!inventory)
        return rows;
    rows.reserve(inventory.value().size());
    for (const InstalledMod& mod : inventory.value()) {
        InventoryRowData row;
        row.key = identityKey(mod.identity.source, mod.identity.id);
        row.name = trimmedQ(mod.descriptor.name);
        if (row.name.isEmpty())
            row.name = qs(mod.identity.id);
        row.source = qs(mod.identity.source);
        row.sourceLabel = sourceLabel(mod.identity.source);
        row.providerId = qs(mod.identity.id);
        row.version = trimmedQ(mod.descriptor.version);
        switch (mod.state) {
        case InstallState::Ready:
            row.stateText = QStringLiteral("Installed");
            break;
        case InstallState::NeedsUpdate:
            row.stateText = QStringLiteral("Installed · update pending");
            break;
        case InstallState::Downloading:
            row.stateText = QStringLiteral("Updating");
            break;
        case InstallState::Broken:
            row.stateText = QStringLiteral("Broken");
            break;
        }
        row.problems = qlist(mod.problems).join(QLatin1Char('\n'));
        rows.push_back(std::move(row));
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const InventoryRowData& a, const InventoryRowData& b) {
                         return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
                     });
    return rows;
}

AcquisitionData acquisitionData(AppRuntime& runtime, const QString& game,
                                const QueryOptions& options) {
    AcquisitionData data;
    data.game = game;
    const services::AcquisitionCoordinator* coordinator = runtime.acquisition();
    if (!coordinator || !options.steamEnabled) {
        data.providerStatus = QStringLiteral("unavailable");
        data.providerMessage = QStringLiteral(
            "Workshop downloads are turned off for this session (--no-steam or read-only mode). "
            "Missing mods stay listed and nothing is subscribed or downloaded.");
        return data;
    }
    const services::AcquisitionSnapshot snapshot = coordinator->snapshot(utf8(game));
    for (const services::JobSnapshot& job : snapshot.jobs) {
        JobRowData row;
        row.jobId = job.job.id;
        row.name = qs(job.name);
        row.providerId = qs(job.job.identity.id);
        row.state = QString::fromLatin1(toString(job.job.state));
        row.stateText = qs(job.stateText);
        row.progress = job.progress;
        row.errorMessage = qs(job.job.errorMessage);
        row.canRetry = job.canRetry;
        row.canOpenFallback = job.canOpenFallback;
        data.jobs.push_back(std::move(row));
    }
    data.providerStatus = QString::fromLatin1(services::toString(snapshot.providerStatus));
    data.providerMessage = qs(snapshot.providerMessage);
    data.summary = qs(snapshot.summary);
    data.active = snapshot.active;
    return data;
}

GameContextData gameContext(AppRuntime& runtime, const QString& game,
                            std::optional<std::int64_t> playsetId, const QueryOptions& options) {
    GameContextData data;
    data.game = game;
    data.list = playsetList(runtime, game);
    std::int64_t chosen = data.list.activeId;
    if (playsetId) {
        const auto found =
            std::find_if(data.list.playsets.begin(), data.list.playsets.end(),
                         [&](const PlaysetSummaryData& p) { return p.id == *playsetId; });
        if (found != data.list.playsets.end())
            chosen = *playsetId;
    }
    data.list.activeId = chosen;
    data.playset = playsetState(runtime, chosen, &data.error);
    data.inventory = inventoryRows(runtime, game);
    data.acquisition = acquisitionData(runtime, game, options);
    data.launch = launchData(runtime, game, data.playset.playsetId, options);
    return data;
}

} // namespace pmm::app::queries
