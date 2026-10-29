#include "app/gui/backend.hpp"

#include "app/core/qt_util.hpp"
#include "app/gui/acquisition_controller.hpp"
#include "app/gui/app_settings.hpp"
#include "app/gui/games_model.hpp"
#include "app/gui/inventory_model.hpp"
#include "app/gui/launch_controller.hpp"
#include "app/gui/mod_details.hpp"
#include "app/gui/mod_filter_model.hpp"
#include "app/gui/mod_list_model.hpp"
#include "app/gui/notification_center.hpp"
#include "app/gui/playsets_model.hpp"
#include "app/gui/share_controller.hpp"
#include "persistence/settings_repository.hpp"

#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTimer>
#include <QtQml/qqml.h>

namespace pmm::app {
namespace {

constexpr qint64 kLaunchRefreshThrottleMs = 5000;

QString levelName(LogLevel level) {
    switch (level) {
    case LogLevel::Error:
        return QStringLiteral("error");
    case LogLevel::Warning:
        return QStringLiteral("warning");
    default:
        return QStringLiteral("info");
    }
}

} // namespace

Backend::Backend(ServiceRunner& runner, BackendOptions options, QObject* parent)
    : QObject(parent), m_runner(runner), m_options(std::move(options)) {
    m_notifications = new NotificationCenter(this);
    m_settings = new AppSettings(&runner, this);
    m_games = new GamesModel(this, this);
    m_playsets = new PlaysetsModel(this, this);
    m_mods = new ModListModel(this, this);
    m_modView = new ModFilterModel(m_mods, this);
    m_mods->setFilterModel(m_modView);
    m_inventory = new InventoryModel(this, this);
    m_details = new ModDetails(m_mods, m_notifications, this);
    m_sharing = new ShareController(this, this);
    m_acquisition = new AcquisitionController(this, this);
    m_launcher = new LaunchController(this, this);

    connect(&m_runner, &ServiceRunner::acquisitionChanged, this, &Backend::onAcquisitionChanged);
    connect(&m_runner, &ServiceRunner::processStarting, m_launcher,
            &LaunchController::processStarting);
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        // A game or the Paradox launcher may have been started or closed meanwhile.
        connect(app, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState s) {
                    if (s == Qt::ApplicationActive && m_initialDone &&
                        (!m_sinceLaunchRefresh.isValid() ||
                         m_sinceLaunchRefresh.elapsed() > kLaunchRefreshThrottleMs))
                        refreshLaunch();
                });
    }
}

Backend::~Backend() = default;

void Backend::registerQmlTypes() {
    const QString reason = QStringLiteral("Provided by the Backend singleton");
    qmlRegisterUncreatableType<GamesModel>("Pmm.Backend", 1, 0, "GamesModel", reason);
    qmlRegisterUncreatableType<PlaysetsModel>("Pmm.Backend", 1, 0, "PlaysetsModel", reason);
    qmlRegisterUncreatableType<ModListModel>("Pmm.Backend", 1, 0, "ModListModel", reason);
    qmlRegisterUncreatableType<ModFilterModel>("Pmm.Backend", 1, 0, "ModFilterModel", reason);
    qmlRegisterUncreatableType<InventoryModel>("Pmm.Backend", 1, 0, "InventoryModel", reason);
    qmlRegisterUncreatableType<ModDetails>("Pmm.Backend", 1, 0, "ModDetails", reason);
    qmlRegisterUncreatableType<ShareController>("Pmm.Backend", 1, 0, "ShareController", reason);
    qmlRegisterUncreatableType<AcquisitionController>("Pmm.Backend", 1, 0, "AcquisitionController",
                                                      reason);
    qmlRegisterUncreatableType<JobsModel>("Pmm.Backend", 1, 0, "JobsModel", reason);
    qmlRegisterUncreatableType<LaunchController>("Pmm.Backend", 1, 0, "LaunchController", reason);
    qmlRegisterUncreatableType<AppSettings>("Pmm.Backend", 1, 0, "AppSettings", reason);
    qmlRegisterUncreatableType<NotificationCenter>("Pmm.Backend", 1, 0, "NotificationCenter",
                                                   reason);
}

QString Backend::appVersion() const {
    return QStringLiteral(PMM_APP_VERSION);
}

int Backend::protocolVersion() const {
    return PMM_SHARE_PROTOCOL_VERSION;
}

bool Backend::steamEnabled() const {
    return m_runner.steamEnabled() && !m_options.readOnly;
}

QueryOptions Backend::queryOptions() const {
    QueryOptions options;
    options.readOnly = m_options.readOnly;
    options.steamEnabled = steamEnabled();
    return options;
}

// --- Start-up and shutdown ----------------------------------------------------------

void Backend::openLogsFolder() {
    if (!m_logsDir.isEmpty() && QFileInfo(m_logsDir).isDir())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_logsDir));
}

void Backend::initialize(const RunnerStart& start) {
    m_dataDir = start.dataDir;
    m_logsDir = start.logsDir;
    // Posted once the event loop runs, so the window's toasts see them.
    for (const StartupNotice& notice : start.notices) {
        QTimer::singleShot(0, this, [this, notice] {
            m_notifications->post(levelName(notice.level), qs(notice.title), qs(notice.text));
        });
    }

    struct Initial {
        std::map<std::string, std::string> settings;
        std::vector<GameRowData> games;
    };
    Initial initial = m_runner.callBlocking([](AppRuntime& runtime) {
        Initial out;
        persistence::SettingsRepository settings(runtime.database());
        for (const std::string& key : AppSettings::storageKeys()) {
            if (auto value = settings.get(key); value && value.value())
                out.settings.emplace(key, *value.value());
        }
        out.games = queries::gameRows(runtime.games().cachedStates());
        return out;
    });
    m_settings->load(initial.settings);
    m_games->setRows(std::move(initial.games));

    const QString game = pickInitialGame();
    if (!game.isEmpty()) {
        m_game = game;
        m_games->setCurrent(game);
        m_settings->setLastGameKey(game);
        const QueryOptions options = queryOptions();
        applyContext(m_runner.callBlocking([game, options](AppRuntime& runtime) {
            return queries::gameContext(runtime, game, std::nullopt, options);
        }));
        m_sinceLaunchRefresh.start();
    }
    rescan(false);
}

void Backend::shutdown() {
    m_settings->save();
}

QString Backend::pickInitialGame() const {
    const auto usable = [this](const QString& key) {
        const GameRowData* row = m_games->find(key);
        return row && row->supported;
    };
    if (!m_options.initialGame.isEmpty() && usable(m_options.initialGame))
        return m_options.initialGame;
    if (usable(m_settings->lastGameKey()))
        return m_settings->lastGameKey();
    for (const GameRowData& row : m_games->rows()) {
        if (row.supported && row.installed)
            return row.key;
    }
    for (const GameRowData& row : m_games->rows()) {
        if (row.supported)
            return row.key;
    }
    return {};
}

void Backend::finishInitialRefresh() {
    if (m_initialDone)
        return;
    m_initialDone = true;
    emit initialRefreshFinished();
}

// --- Context --------------------------------------------------------------------

void Backend::applyContext(GameContextData data) {
    if (data.game != m_game)
        return;
    m_playsets->setList(data.list);
    m_playsetId = data.playset.playsetId;
    if (data.playset.playsetId != m_mods->playsetId())
        m_mods->resetTo(std::move(data.playset));
    else
        m_mods->applyState(std::move(data.playset));
    m_inventory->setRows(std::move(data.inventory));
    m_inventory->setPlaysetKeys(m_mods->keys());
    m_acquisition->apply(std::move(data.acquisition));
    m_launcher->apply(std::move(data.launch));
    m_sinceLaunchRefresh.restart();
    if (!data.error.isEmpty())
        m_notifications->warning(tr("Could not load the playset"), data.error);
    emit contextLoaded();
}

void Backend::clearContext() {
    m_playsetId = -1;
    m_playsets->clear();
    m_mods->resetTo({});
    m_inventory->setRows({});
    m_inventory->setPlaysetKeys({});
    m_acquisition->apply({});
    m_launcher->apply({});
    m_details->setRow(-1);
}

void Backend::selectGame(const QString& key) {
    const GameRowData* row = m_games->find(key);
    if (!row || !row->supported || key == m_game)
        return;
    if (!m_game.isEmpty())
        m_viewStates[m_game] = {m_modView->search(), m_modView->statusFilter(),
                                m_modView->sortMode()};
    const ViewState view = m_viewStates.count(key) ? m_viewStates.at(key) : ViewState{};
    m_modView->setSearch(view.search);
    m_modView->setStatusFilter(view.statusFilter);
    m_modView->setSortMode(view.sortMode);

    m_game = key;
    m_games->setCurrent(key);
    m_settings->setLastGameKey(key);
    clearContext();
    const quint64 generation = ++m_contextGeneration;
    const QueryOptions options = queryOptions();
    m_runner.call(
        this,
        [key, options](AppRuntime& runtime) {
            if (Status saved = runtime.playsets().setLastGameKey(utf8(key)); !saved)
                logWarning("app.backend", "saving the last game failed",
                           {{"error", saved.error().message}});
            return queries::gameContext(runtime, key, std::nullopt, options);
        },
        [this, generation](GameContextData data) {
            if (generation == m_contextGeneration)
                applyContext(std::move(data));
        });
}

void Backend::reloadContext(std::optional<std::int64_t> playsetId, std::function<void()> then) {
    if (m_game.isEmpty()) {
        if (then)
            then();
        return;
    }
    const quint64 generation = ++m_contextGeneration;
    const QString game = m_game;
    const QueryOptions options = queryOptions();
    m_runner.call(
        this,
        [game, playsetId, options](AppRuntime& runtime) {
            return queries::gameContext(runtime, game, playsetId, options);
        },
        [this, generation, then](GameContextData data) {
            if (generation == m_contextGeneration)
                applyContext(std::move(data));
            if (then)
                then();
        });
}

void Backend::rescan(bool userRequested) {
    if (m_games->scanning())
        return;
    m_games->setScanning(true);
    const bool steam = steamEnabled();
    ServiceRunner* runner = &m_runner;
    struct Scan {
        std::vector<GameRowData> rows;
        int installed = 0;
        int mods = 0;
    };
    m_runner.call(
        this,
        [steam, runner](AppRuntime& runtime) {
            Scan scan;
            const std::vector<services::GameState> states = runtime.games().refreshAll();
            for (const services::GameState& state : states) {
                if (state.installation) {
                    ++scan.installed;
                    scan.mods += static_cast<int>(state.inventory.size());
                }
                // Resume unfinished Workshop jobs (restart reconciliation).
                if (steam && state.supported && runtime.acquisition())
                    runner->report(runtime.acquisition()->start(state.key));
            }
            scan.rows = queries::gameRows(states);
            return scan;
        },
        [this, userRequested](Scan scan) {
            m_games->setScanning(false);
            QStringList problems;
            for (const GameRowData& row : scan.rows) {
                if (!row.scanError.isEmpty())
                    problems.push_back(row.scanError);
            }
            m_games->setRows(std::move(scan.rows));
            if (!problems.isEmpty())
                m_notifications->warning(tr("Scan problems"), problems.join(QLatin1Char('\n')));
            if (userRequested)
                m_notifications->info(
                    tr("Scan complete"),
                    tr("Found %1 with %2.")
                        .arg(countText(scan.installed, "installed game", "installed games"),
                             countText(scan.mods, "installed mod", "installed mods")));
            const GameRowData* current = m_games->find(m_game);
            if (!current || !current->supported) {
                const QString game = pickInitialGame();
                if (!game.isEmpty())
                    selectGame(game);
                finishInitialRefresh();
                return;
            }
            reloadContext(m_playsetId >= 0 ? std::optional<std::int64_t>(m_playsetId)
                                           : std::nullopt,
                          [this] { finishInitialRefresh(); });
        });
}

bool Backend::locateGame(const QString& key, const QUrl& installDir, const QUrl& userDataDir) {
    const QString install = installDir.isLocalFile() ? installDir.toLocalFile() : QString();
    if (install.isEmpty()) {
        m_notifications->error(tr("Installation not recognised"),
                               tr("Choose the folder that contains the game executable."));
        return false;
    }
    const QString userData = userDataDir.isLocalFile() ? userDataDir.toLocalFile() : QString();
    m_runner.call(
        this,
        [key, install,
         userData](AppRuntime& runtime) -> std::pair<std::optional<Error>, GameRowData> {
            auto state =
                runtime.games().setManualLocation(utf8(key), toPath(install), toPath(userData));
            if (!state)
                return {state.error(), {}};
            return {std::nullopt, queries::gameRow(state.value())};
        },
        [this, key](std::pair<std::optional<Error>, GameRowData> result) {
            if (result.first) {
                m_notifications->error(tr("Installation not recognised"),
                                       qs(result.first->message));
                return;
            }
            m_games->updateRow(result.second);
            m_notifications->info(
                tr("Installation located"),
                tr("%1 will use %2.").arg(result.second.name, result.second.installPath));
            if (key == m_game)
                reloadContext(m_playsetId >= 0 ? std::optional<std::int64_t>(m_playsetId)
                                               : std::nullopt);
            else
                selectGame(key);
        });
    return true;
}

void Backend::clearManualLocation(const QString& key) {
    m_runner.call(
        this,
        [key](AppRuntime& runtime) -> std::pair<std::optional<Error>, GameRowData> {
            auto state = runtime.games().clearManualLocation(utf8(key));
            if (!state)
                return {state.error(), {}};
            return {std::nullopt, queries::gameRow(state.value())};
        },
        [this, key](std::pair<std::optional<Error>, GameRowData> result) {
            if (result.first) {
                m_notifications->error(tr("Could not reset the location"),
                                       qs(result.first->message));
                return;
            }
            m_games->updateRow(result.second);
            m_notifications->info(
                tr("Automatic detection restored"),
                result.second.installed
                    ? tr("%1 was found at %2.").arg(result.second.name, result.second.installPath)
                    : tr("%1 was not found in the Steam libraries.").arg(result.second.name));
            if (key == m_game)
                reloadContext();
        });
}

bool Backend::importCurrentConfiguration() {
    if (m_game.isEmpty())
        return false;
    const QString game = m_game;
    const QueryOptions options = queryOptions();
    const quint64 generation = ++m_contextGeneration;
    struct Outcome {
        std::optional<Error> error;
        GameContextData context;
    };
    m_runner.call(
        this,
        [game, options](AppRuntime& runtime) {
            Outcome outcome;
            auto id = runtime.games().importCurrentConfiguration(utf8(game));
            if (!id) {
                outcome.error = id.error();
                return outcome;
            }
            outcome.context = queries::gameContext(runtime, game, id.value(), options);
            return outcome;
        },
        [this, generation](Outcome outcome) {
            if (outcome.error) {
                m_notifications->error(tr("Could not use the game configuration"),
                                       qs(outcome.error->message));
                return;
            }
            const QString name = outcome.context.playset.name;
            const int total = static_cast<int>(outcome.context.playset.rows.size());
            if (generation == m_contextGeneration)
                applyContext(std::move(outcome.context));
            m_notifications->info(tr("Playset created"),
                                  tr("“%1” has the %2 enabled in the game's own configuration, "
                                     "in the same order.")
                                      .arg(name, countText(total, "mod", "mods")));
        });
    return true;
}

void Backend::activatePlayset(std::int64_t playsetId) {
    if (m_game.isEmpty())
        return;
    m_playsetId = playsetId;
    m_playsets->setActive(playsetId);
    const QString game = m_game;
    const QueryOptions options = queryOptions();
    const quint64 generation = ++m_contextGeneration;
    m_runner.call(
        this,
        [game, playsetId, options](AppRuntime& runtime) {
            if (Status saved = runtime.playsets().setActivePlayset(utf8(game), playsetId); !saved)
                logWarning("app.backend", "saving the active playset failed",
                           {{"error", saved.error().message}});
            return queries::gameContext(runtime, game, playsetId, options);
        },
        [this, generation](GameContextData data) {
            if (generation == m_contextGeneration)
                applyContext(std::move(data));
        });
}

void Backend::createPlayset(const QString& name, std::optional<std::int64_t> duplicateOf) {
    if (m_game.isEmpty())
        return;
    const QString game = m_game;
    const QueryOptions options = queryOptions();
    const quint64 generation = ++m_contextGeneration;
    struct Outcome {
        std::optional<Error> error;
        GameContextData context;
    };
    m_runner.call(
        this,
        [game, name, duplicateOf, options](AppRuntime& runtime) {
            Outcome outcome;
            auto id = duplicateOf ? runtime.playsets().duplicate(*duplicateOf, utf8(name))
                                  : runtime.playsets().create(utf8(game), utf8(name));
            if (!id) {
                outcome.error = id.error();
                return outcome;
            }
            if (Status active = runtime.playsets().setActivePlayset(utf8(game), id.value());
                !active)
                outcome.error = active.error();
            outcome.context = queries::gameContext(runtime, game, id.value(), options);
            return outcome;
        },
        [this, generation, duplicateOf](Outcome outcome) {
            if (outcome.error) {
                m_notifications->error(duplicateOf ? tr("Could not duplicate the playset")
                                                   : tr("Could not create the playset"),
                                       qs(outcome.error->message));
                if (outcome.context.game.isEmpty())
                    return;
            }
            if (generation == m_contextGeneration)
                applyContext(std::move(outcome.context));
        });
}

void Backend::renamePlayset(std::int64_t playsetId, const QString& name) {
    const QString game = m_game;
    struct Outcome {
        std::optional<Error> error;
        PlaysetListData list;
    };
    m_runner.call(
        this,
        [playsetId, name, game](AppRuntime& runtime) {
            Outcome outcome;
            if (Status renamed = runtime.playsets().rename(playsetId, utf8(name)); !renamed)
                outcome.error = renamed.error();
            outcome.list = queries::playsetList(runtime, game);
            return outcome;
        },
        [this, game](Outcome outcome) {
            if (outcome.error)
                m_notifications->warning(tr("Could not rename the playset"),
                                         qs(outcome.error->message));
            if (game != m_game)
                return;
            outcome.list.activeId = m_playsetId;
            m_playsets->setList(outcome.list);
        });
}

void Backend::removePlayset(std::int64_t playsetId) {
    const QString game = m_game;
    const QueryOptions options = queryOptions();
    const quint64 generation = ++m_contextGeneration;
    struct Outcome {
        std::optional<Error> error;
        std::int64_t historyId = 0;
        QString name;
        GameContextData context;
    };
    m_runner.call(
        this,
        [playsetId, game, options](AppRuntime& runtime) {
            Outcome outcome;
            if (auto playset = runtime.playsets().playset(playsetId))
                outcome.name = qs(playset.value().name);
            auto removed = runtime.playsets().remove(playsetId);
            if (!removed)
                outcome.error = removed.error();
            else
                outcome.historyId = removed.value();
            outcome.context = queries::gameContext(runtime, game, std::nullopt, options);
            return outcome;
        },
        [this, generation](Outcome outcome) {
            if (outcome.error) {
                m_notifications->error(tr("Could not delete the playset"),
                                       qs(outcome.error->message));
            } else {
                m_playsets->rememberDeleted(outcome.historyId);
                m_notifications->info(
                    tr("Playset deleted"),
                    tr("“%1” was deleted. Use Restore to bring it back.").arg(outcome.name));
            }
            if (generation == m_contextGeneration)
                applyContext(std::move(outcome.context));
        });
}

void Backend::restoreHistory(std::int64_t historyId, bool lastDeleted) {
    const QString game = m_game;
    const QueryOptions options = queryOptions();
    const quint64 generation = ++m_contextGeneration;
    struct Outcome {
        std::optional<Error> error;
        GameContextData context;
    };
    m_runner.call(
        this,
        [historyId, lastDeleted, game, options](AppRuntime& runtime) {
            Outcome outcome;
            auto restored = lastDeleted ? runtime.playsets().restoreLastDeleted(utf8(game))
                                        : runtime.playsets().restoreHistory(historyId);
            if (!restored) {
                outcome.error = restored.error();
                outcome.context = queries::gameContext(runtime, game, std::nullopt, options);
            } else {
                outcome.context = queries::gameContext(runtime, game, restored.value(), options);
            }
            return outcome;
        },
        [this, generation](Outcome outcome) {
            if (outcome.error)
                m_notifications->error(tr("Could not restore the playset"),
                                       qs(outcome.error->message));
            else
                m_notifications->info(
                    tr("Playset restored"),
                    tr("“%1” is back and active.").arg(outcome.context.playset.name));
            if (generation == m_contextGeneration)
                applyContext(std::move(outcome.context));
        });
}

void Backend::refreshInventory() {
    if (m_game.isEmpty())
        return;
    const QString game = m_game;
    m_runner.call(
        this, [game](AppRuntime& runtime) { return queries::inventoryRows(runtime, game); },
        [this, game](std::vector<InventoryRowData> rows) {
            if (game != m_game)
                return;
            m_inventory->setRows(std::move(rows));
            m_inventory->setPlaysetKeys(m_mods->keys());
        });
}

void Backend::refreshAcquisition(bool includeLaunch) {
    if (m_game.isEmpty())
        return;
    if (m_acquisitionInFlight) {
        m_acquisitionPending = true;
        m_acquisitionPendingLaunch = m_acquisitionPendingLaunch || includeLaunch;
        return;
    }
    if (!includeLaunch && m_sinceLaunchRefresh.isValid() &&
        m_sinceLaunchRefresh.elapsed() > kLaunchRefreshThrottleMs)
        includeLaunch = true;
    m_acquisitionInFlight = true;
    const QString game = m_game;
    const std::int64_t playsetId = m_playsetId;
    const QueryOptions options = queryOptions();
    struct Outcome {
        AcquisitionData acquisition;
        PlaysetStateData playset;
        std::optional<LaunchData> launch;
    };
    m_runner.call(
        this,
        [game, playsetId, options, includeLaunch](AppRuntime& runtime) {
            Outcome outcome;
            outcome.acquisition = queries::acquisitionData(runtime, game, options);
            outcome.playset = queries::playsetState(runtime, playsetId);
            if (includeLaunch)
                outcome.launch = queries::launchData(runtime, game, playsetId, options);
            return outcome;
        },
        [this, game, playsetId](Outcome outcome) {
            m_acquisitionInFlight = false;
            if (game == m_game) {
                m_acquisition->apply(std::move(outcome.acquisition));
                if (playsetId == m_playsetId && outcome.playset.playsetId == playsetId) {
                    m_mods->applyState(std::move(outcome.playset));
                    m_playsets->updateCounts(playsetId, m_mods->count(), m_mods->enabledCount(),
                                             {});
                }
                if (outcome.launch) {
                    m_launcher->apply(std::move(*outcome.launch));
                    m_sinceLaunchRefresh.restart();
                }
            }
            if (std::exchange(m_acquisitionPending, false))
                refreshAcquisition(std::exchange(m_acquisitionPendingLaunch, false));
        });
}

void Backend::refreshLaunch() {
    if (m_game.isEmpty())
        return;
    m_sinceLaunchRefresh.restart();
    const QString game = m_game;
    const std::int64_t playsetId = m_playsetId;
    const QueryOptions options = queryOptions();
    m_runner.call(
        this,
        [game, playsetId, options](AppRuntime& runtime) {
            return queries::launchData(runtime, game, playsetId, options);
        },
        [this, game, playsetId](LaunchData data) {
            if (game == m_game && playsetId == m_playsetId)
                m_launcher->apply(std::move(data));
        });
}

void Backend::rescanCurrentMods() {
    if (m_game.isEmpty())
        return;
    const QString game = m_game;
    m_games->setScanning(true);
    m_runner.call(
        this,
        [game](AppRuntime& runtime) -> std::pair<std::optional<Error>, GameRowData> {
            auto state = runtime.games().rescanMods(utf8(game));
            if (!state)
                return {state.error(), {}};
            return {std::nullopt, queries::gameRow(state.value())};
        },
        [this, game](std::pair<std::optional<Error>, GameRowData> result) {
            m_games->setScanning(false);
            if (result.first) {
                m_notifications->warning(tr("Rescan failed"), qs(result.first->message));
                return;
            }
            m_games->updateRow(result.second);
            m_notifications->info(
                tr("Rescan finished"),
                tr("%1 found.")
                    .arg(countText(result.second.modCount, "installed mod", "installed mods")));
            if (game == m_game)
                reloadContext(m_playsetId >= 0 ? std::optional<std::int64_t>(m_playsetId)
                                               : std::nullopt);
        });
}

void Backend::playsetEdited(const std::optional<LaunchData>& launch) {
    m_playsets->updateCounts(m_playsetId, m_mods->count(), m_mods->enabledCount(), {});
    m_inventory->setPlaysetKeys(m_mods->keys());
    if (launch) {
        m_launcher->apply(*launch);
        m_sinceLaunchRefresh.restart();
    }
}

void Backend::onAcquisitionChanged(const QStringList& rescanned) {
    if (!rescanned.isEmpty()) {
        m_runner.call(
            this,
            [](AppRuntime& runtime) { return queries::gameRows(runtime.games().cachedStates()); },
            [this](std::vector<GameRowData> rows) { m_games->setRows(std::move(rows)); });
        if (rescanned.contains(m_game)) {
            reloadContext(m_playsetId >= 0 ? std::optional<std::int64_t>(m_playsetId)
                                           : std::nullopt);
            return;
        }
    }
    refreshAcquisition();
}

} // namespace pmm::app
