#pragma once

// Plain, GUI-ready snapshots of service state, built on the services worker
// (queries below) and copied to the GUI thread, where the models and
// controllers turn them into Qt model updates. Strings are already QString so
// the GUI thread does no conversion work for large playsets.

#include "app/core/app_runtime.hpp"
#include "core/model.hpp"
#include "core/playset_ops.hpp"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

namespace pmm::app {

struct GameRowData {
    QString key;
    QString name;
    bool supported = false;
    bool installed = false;
    bool manualLocation = false;
    QString statusText; // "Installed · v4.5.2", "Not found", "Not supported yet"
    QString version;
    QString installPath;  // display
    QString userDataPath; // display
    QString scanError;
    int modCount = 0;
};

struct DependencyData {
    QString name;
    QString state; // ok | missing | not_in_playset | disabled | order | unchecked
    QString text;
};

struct ModRowData {
    PlaysetEntry entry; // core entry (optimistic edits reuse core/playset_ops)
    QString key;        // "source:id"
    QString name;
    QString source;
    QString sourceLabel;
    QString providerId;
    QString status;
    QString statusText;
    QStringList warnings;
    double progress = -1.0;
    bool blocking = false;
    // Details panel
    QString version;
    QString supportedVersion;
    QStringList tags;
    std::vector<DependencyData> dependencies;
    QStringList problems;
    QString workshopUrl;
    QString contentPath;
    bool canOpenFolder = false;

    bool enabled() const noexcept { return entry.enabled; }
};

struct CountsData {
    int total = 0;
    int enabled = 0;
    int ready = 0;
    int missing = 0;
    int warning = 0;
    int blocking = 0;
};

struct UndoData {
    bool canUndo = false;
    bool canRedo = false;
    QString undoText;
    QString redoText;
};

struct PlaysetSummaryData {
    std::int64_t id = 0;
    QString name;
    int entryCount = 0;
    int enabledCount = 0;
    QString updatedText;
};

struct HistoryData {
    std::int64_t historyId = 0;
    QString reason; // display text
    QString playsetName;
    QString createdText;
};

struct LaunchData {
    bool canPlay = false;
    QStringList blockers;
    QStringList warnings;
    bool interrupted = false;
    bool canRestoreBackup = false;
};

struct InventoryRowData {
    QString key;
    QString name;
    QString source;
    QString sourceLabel;
    QString providerId;
    QString version;
    QString stateText;
    QString problems;
};

struct JobRowData {
    std::int64_t jobId = 0;
    QString name;
    QString providerId;
    QString state; // AcquisitionState name
    QString stateText;
    double progress = -1.0;
    QString errorMessage;
    bool canRetry = false;
    bool canOpenFallback = false;
};

struct AcquisitionData {
    QString game;
    std::vector<JobRowData> jobs;
    QString providerStatus = QStringLiteral("unknown");
    QString providerMessage;
    QString summary;
    bool active = false;
};

struct PlaysetStateData {
    std::int64_t playsetId = -1;
    QString game;
    QString name;
    std::vector<ModRowData> rows;
    CountsData counts;
    UndoData undo;
};

struct PlaysetListData {
    std::vector<PlaysetSummaryData> playsets;
    std::int64_t activeId = -1;
    std::optional<std::int64_t> lastDeletedHistoryId;
    std::vector<HistoryData> history;
};

// Everything the window shows for one game.
struct GameContextData {
    QString game;
    PlaysetListData list;
    PlaysetStateData playset; // playsetId -1 without an active playset
    std::vector<InventoryRowData> inventory;
    AcquisitionData acquisition;
    LaunchData launch;
    QString error; // loading problem, shown as a notification
};

struct QueryOptions {
    // No write probes in game folders (the launch check runs without them)
    // and no launching: verification runs against real game data.
    bool readOnly = false;
    bool steamEnabled = true;
};

namespace queries {

std::vector<GameRowData> gameRows(const std::vector<services::GameState>& states);
GameRowData gameRow(const services::GameState& state);

PlaysetListData playsetList(AppRuntime& runtime, const QString& game);
// Missing playset -> playsetId -1 (and error set when it could not be read).
PlaysetStateData playsetState(AppRuntime& runtime, std::int64_t playsetId,
                              QString* error = nullptr);
LaunchData launchData(AppRuntime& runtime, const QString& game, std::int64_t playsetId,
                      const QueryOptions& options);
std::vector<InventoryRowData> inventoryRows(AppRuntime& runtime, const QString& game);
AcquisitionData acquisitionData(AppRuntime& runtime, const QString& game,
                                const QueryOptions& options);
// Uses `playsetId` when it is a live playset of the game, otherwise the
// stored active playset (or the first one).
GameContextData gameContext(AppRuntime& runtime, const QString& game,
                            std::optional<std::int64_t> playsetId, const QueryOptions& options);

// "Today 14:02", "Yesterday 09:15", "12 Oct 2026".
QString relativeTime(std::int64_t unixMs);
QString historyReasonText(const std::string& reason);
QString statusTextFor(const EntryReadiness& readiness, const std::optional<AcquisitionJob>& job);
// The view state as the core reorder rules see it.
ViewFilter toViewFilter(const QString& search, const QString& statusFilter,
                        const QString& sortMode);

} // namespace queries
} // namespace pmm::app
