#pragma once

// The application's composition root, shared by pmm-app and the CLI: resolves
// the application folders (standard per-user locations, or one folder given
// with --profile), installs the file log sink, opens the database and the
// configuration-transaction journal, builds the ServiceContext with the real
// game adapters and discovery environment, and constructs one instance of each
// service. Startup maintenance applies the retention rules (playset history
// older than 30 days beyond the newest 10 per playset, finished acquisition
// jobs older than 7 days, finished configuration transactions beyond the
// newest 10).
//
// Interrupted configuration transactions are deliberately NOT recovered here:
// they are reported as a startup notice and offered to the user (Recover).
//
// Like the services, an AppRuntime is not thread-safe: create and use it on one
// thread (the ServiceRunner's worker in the GUI application).

#include "core/log.hpp"
#include "core/result.hpp"
#include "games/game_adapter.hpp"
#include "persistence/database.hpp"
#include "platform/file_transaction.hpp"
#include "services/acquisition_coordinator.hpp"
#include "services/app_paths.hpp"
#include "services/game_service.hpp"
#include "services/launch_service.hpp"
#include "services/playset_service.hpp"
#include "services/service_context.hpp"
#include "services/share_service.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pmm::app {

class FileLogSink;

struct RuntimeOptions {
    std::optional<std::filesystem::path> profileDir; // --profile <dir>
    bool verbose = false;                            // debug-level log records
    bool installLogSink = true;
    // Test seams; the defaults are the real adapters, discovery environment and
    // system clock/process/fingerprint functions.
    std::optional<std::vector<const games::GameAdapter*>> adapters;
    std::optional<games::DiscoveryEnvironment> environment;
    std::function<void(services::ServiceContext&)> configureContext;
};

struct RuntimeHosts {
    // Null: no acquisition coordinator (Workshop downloads unavailable).
    services::WorkshopSessionHost* sessions = nullptr;
    services::WorkshopMetadataHost* metadata = nullptr;
    services::CoordinatorOptions coordinator;
};

struct StartupNotice {
    LogLevel level = LogLevel::Info;
    std::string title;
    std::string text;
};

class AppRuntime {
public:
    static constexpr std::int64_t kHistoryRetentionMs = 30ll * 24 * 60 * 60 * 1000;
    static constexpr int kHistoryKeepPerPlayset = 10;
    static constexpr std::int64_t kJobRetentionMs = 7ll * 24 * 60 * 60 * 1000;
    static constexpr std::size_t kKeptTransactions = 10;

    // Fails only for conditions the application cannot run with (folders that
    // cannot be created, a database that cannot be opened or was written by a
    // newer version); the error message is shown to the user as is.
    static Result<std::unique_ptr<AppRuntime>> create(const RuntimeOptions& options,
                                                      const RuntimeHosts& hosts = {});
    ~AppRuntime();
    AppRuntime(const AppRuntime&) = delete;
    AppRuntime& operator=(const AppRuntime&) = delete;

    const services::AppPaths& paths() const noexcept { return m_paths; }
    persistence::Database& database() noexcept { return *m_database; }
    platform::FileTransactionManager& transactions() noexcept { return *m_transactions; }
    services::ServiceContext& context() noexcept { return *m_context; }
    services::GameService& games() noexcept { return *m_games; }
    services::PlaysetService& playsets() noexcept { return *m_playsets; }
    services::ShareService& sharing() noexcept { return *m_sharing; }
    services::LaunchService& launcher() noexcept { return *m_launcher; }
    // Null when no session host was given.
    services::AcquisitionCoordinator* acquisition() noexcept { return m_acquisition.get(); }

    const std::vector<StartupNotice>& notices() const noexcept { return m_notices; }
    // Current log file, empty without the file sink.
    std::filesystem::path logFile() const;

    // Called right before the game process is created by a launch (after the
    // configuration was written), so the UI can show "starting".
    void setBeforeProcessStart(std::function<void()> hook);

    // Persists acquisition progress and closes every helper session.
    void shutdown();

private:
    AppRuntime() = default;
    void runMaintenance();

    services::AppPaths m_paths;
    std::shared_ptr<FileLogSink> m_logSink;
    bool m_ownsLogSink = false;
    std::unique_ptr<persistence::Database> m_database;
    std::unique_ptr<platform::FileTransactionManager> m_transactions;
    std::unique_ptr<services::ServiceContext> m_context;
    std::unique_ptr<services::GameService> m_games;
    std::unique_ptr<services::PlaysetService> m_playsets;
    std::unique_ptr<services::ShareService> m_sharing;
    std::unique_ptr<services::AcquisitionCoordinator> m_acquisition;
    std::unique_ptr<services::LaunchService> m_launcher;
    std::shared_ptr<std::function<void()>> m_beforeProcessStart;
    std::vector<StartupNotice> m_notices;
    bool m_shutDown = false;
};

} // namespace pmm::app
