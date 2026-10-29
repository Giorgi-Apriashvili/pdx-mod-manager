#pragma once

// The QML singleton "Backend" (docs/ui-contract.md): owns the models and
// controllers and keeps the window's context - the current game and its
// active playset - in step with the services. Every service call goes
// through the ServiceRunner; results come back as GameContextData and friends
// and are applied as incremental model updates on the GUI thread.

#include "app/core/service_runner.hpp"
#include "app/core/view_data.hpp"
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

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>

namespace pmm::app {

struct BackendOptions {
    // No writes to game folders and no launching (verification against real
    // game data); implies Workshop downloads off.
    bool readOnly = false;
    QString initialGame; // overrides the last selected game
};

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(pmm::app::GamesModel* games READ games CONSTANT)
    Q_PROPERTY(pmm::app::PlaysetsModel* playsets READ playsets CONSTANT)
    Q_PROPERTY(pmm::app::ModListModel* mods READ mods CONSTANT)
    Q_PROPERTY(pmm::app::ModFilterModel* modView READ modView CONSTANT)
    Q_PROPERTY(pmm::app::InventoryModel* inventory READ inventory CONSTANT)
    Q_PROPERTY(pmm::app::ModDetails* details READ details CONSTANT)
    Q_PROPERTY(pmm::app::ShareController* sharing READ sharing CONSTANT)
    Q_PROPERTY(pmm::app::AcquisitionController* acquisition READ acquisition CONSTANT)
    Q_PROPERTY(pmm::app::LaunchController* launcher READ launcher CONSTANT)
    Q_PROPERTY(pmm::app::AppSettings* settings READ settings CONSTANT)
    Q_PROPERTY(pmm::app::NotificationCenter* notifications READ notifications CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(int protocolVersion READ protocolVersion CONSTANT)
    Q_PROPERTY(bool readOnly READ readOnly CONSTANT)
    // Application folders of this profile (display; set before QML loads).
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(QString logsDir READ logsDir CONSTANT)

public:
    Backend(ServiceRunner& runner, BackendOptions options, QObject* parent = nullptr);
    ~Backend() override;

    // Registers the C++ types with QML (uncreatable, module Pmm.Backend).
    static void registerQmlTypes();

    GamesModel* games() const { return m_games; }
    PlaysetsModel* playsets() const { return m_playsets; }
    ModListModel* mods() const { return m_mods; }
    ModFilterModel* modView() const { return m_modView; }
    InventoryModel* inventory() const { return m_inventory; }
    ModDetails* details() const { return m_details; }
    ShareController* sharing() const { return m_sharing; }
    AcquisitionController* acquisition() const { return m_acquisition; }
    LaunchController* launcher() const { return m_launcher; }
    AppSettings* settings() const { return m_settings; }
    NotificationCenter* notifications() const { return m_notifications; }
    QString appVersion() const;
    int protocolVersion() const;
    bool readOnly() const { return m_options.readOnly; }
    QString dataDir() const { return m_dataDir; }
    QString logsDir() const { return m_logsDir; }
    Q_INVOKABLE void openLogsFolder();

    ServiceRunner& runner() { return m_runner; }
    bool steamEnabled() const;
    QueryOptions queryOptions() const;
    QString currentGame() const { return m_game; }
    std::int64_t activePlaysetId() const { return m_playsetId; }
    bool initialRefreshDone() const { return m_initialDone; }

    // Start-up: posts the startup notices, loads settings, cached game states
    // and the initial game's context (blocking, database only), then starts
    // the first rescan in the background (initialRefreshFinished()).
    void initialize(const RunnerStart& start);
    // Saves the settings; call before the runner shuts down.
    void shutdown();

    // --- Context, used by the models and controllers ---------------------------
    void selectGame(const QString& key);
    void rescan(bool userRequested);
    bool locateGame(const QString& key, const QUrl& installDir, const QUrl& userDataDir);
    void clearManualLocation(const QString& key);
    bool importCurrentConfiguration();
    void activatePlayset(std::int64_t playsetId);
    void createPlayset(const QString& name, std::optional<std::int64_t> duplicateOf);
    void renamePlayset(std::int64_t playsetId, const QString& name);
    void removePlayset(std::int64_t playsetId);
    void restoreHistory(std::int64_t historyId, bool lastDeleted);
    // Without a playset id: the stored active playset. Calls run in order, so
    // that includes every activation, creation or import submitted earlier.
    void reloadContext(std::optional<std::int64_t> playsetId = std::nullopt,
                       std::function<void()> then = {});
    void refreshInventory();
    void refreshAcquisition(bool includeLaunch = false);
    void refreshLaunch();
    void rescanCurrentMods();
    // After ModListModel applied an edit's authoritative state.
    void playsetEdited(const std::optional<LaunchData>& launch);

signals:
    void initialRefreshFinished();
    void contextLoaded();

private:
    struct ViewState {
        QString search;
        QString statusFilter = QStringLiteral("all");
        QString sortMode = QStringLiteral("order");
    };

    void applyContext(GameContextData data);
    void clearContext();
    void onAcquisitionChanged(const QStringList& rescanned);
    QString pickInitialGame() const;
    void finishInitialRefresh();

    ServiceRunner& m_runner;
    BackendOptions m_options;
    NotificationCenter* m_notifications = nullptr;
    AppSettings* m_settings = nullptr;
    GamesModel* m_games = nullptr;
    PlaysetsModel* m_playsets = nullptr;
    ModListModel* m_mods = nullptr;
    ModFilterModel* m_modView = nullptr;
    InventoryModel* m_inventory = nullptr;
    ModDetails* m_details = nullptr;
    ShareController* m_sharing = nullptr;
    AcquisitionController* m_acquisition = nullptr;
    LaunchController* m_launcher = nullptr;

    QString m_dataDir;
    QString m_logsDir;
    QString m_game;
    std::int64_t m_playsetId = -1;
    quint64 m_contextGeneration = 0;
    std::map<QString, ViewState> m_viewStates;
    bool m_initialDone = false;
    bool m_acquisitionInFlight = false;
    bool m_acquisitionPending = false;
    bool m_acquisitionPendingLaunch = false;
    QElapsedTimer m_sinceLaunchRefresh;
    QTimer m_trailingLaunchRefresh;
};

} // namespace pmm::app
