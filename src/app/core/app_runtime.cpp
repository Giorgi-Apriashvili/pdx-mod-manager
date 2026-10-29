#include "app/core/app_runtime.hpp"

#include "app/core/file_log_sink.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "platform/fs.hpp"

#include <utility>

namespace pmm::app {
namespace {

constexpr const char* kComponent = "app.runtime";

std::vector<const games::GameAdapter*> realAdapters() {
    std::vector<const games::GameAdapter*> out;
    for (const auto& adapter : games::adapters())
        out.push_back(adapter.get());
    return out;
}

} // namespace

Result<std::unique_ptr<AppRuntime>> AppRuntime::create(const RuntimeOptions& options,
                                                       const RuntimeHosts& hosts) {
    std::unique_ptr<AppRuntime> runtime(new AppRuntime());
    runtime->m_paths = options.profileDir ? services::AppPaths::fromProfileDir(*options.profileDir)
                                          : services::AppPaths::standard();
    const services::AppPaths& paths = runtime->m_paths;
    if (Status created = paths.createDirectories(); !created) {
        return Error(created.error().code,
                     "The application folders could not be created. " + created.error().message,
                     created.error().detail);
    }

    setMinimumLogLevel(options.verbose ? LogLevel::Debug : LogLevel::Info);
    if (options.installLogSink) {
        auto sink = std::make_shared<FileLogSink>(paths.logsDir);
        setLogSink([sink](const LogRecord& record) { sink->write(record); });
        runtime->m_logSink = std::move(sink);
        runtime->m_ownsLogSink = true;
    }
    logInfo(kComponent, "opening application profile",
            {{"data", platform::pathToUtf8(paths.dataDir)},
             {"profile", options.profileDir ? "custom" : "standard"}});

    auto database = persistence::Database::open(paths.databaseFile);
    if (!database) {
        const Error& error = database.error();
        logError(kComponent, "opening the database failed",
                 {{"error", error.message}, {"detail", error.detail}});
        std::string message = error.message;
        if (error.code != ErrorCode::UnsupportedVersion)
            message = "The application database could not be opened. " + message;
        message += " Database file: " + platform::pathToUtf8(paths.databaseFile);
        return Error(error.code, std::move(message), error.detail);
    }
    runtime->m_database = std::move(database).value();
    if (const auto& backup = runtime->m_database->upgradeBackupPath(); !backup.empty()) {
        runtime->m_notices.push_back(
            {LogLevel::Info, "Database upgraded",
             "The application database was upgraded to the current version. A copy of the "
             "previous version was saved as " +
                 platform::pathToUtf8(backup.filename()) + "."});
    }

    runtime->m_transactions =
        std::make_unique<platform::FileTransactionManager>(paths.transactionsDir);
    runtime->m_context =
        std::make_unique<services::ServiceContext>(*runtime->m_database, *runtime->m_transactions);
    services::ServiceContext& context = *runtime->m_context;
    context.adapters = options.adapters ? *options.adapters : realAdapters();
    context.environment =
        options.environment ? *options.environment : games::DiscoveryEnvironment::current();
    context.paths = paths;
    services::useSystemDefaults(context);
    if (options.configureContext)
        options.configureContext(context);
    runtime->m_beforeProcessStart = std::make_shared<std::function<void()>>();
    if (context.startProcess) {
        context.startProcess = [hook = runtime->m_beforeProcessStart,
                                start = context.startProcess](const platform::ProcessSpec& spec) {
            if (*hook)
                (*hook)();
            return start(spec);
        };
    }

    runtime->m_games = std::make_unique<services::GameService>(context);
    runtime->m_playsets = std::make_unique<services::PlaysetService>(context);
    runtime->m_sharing = std::make_unique<services::ShareService>(context);
    if (hosts.sessions) {
        runtime->m_acquisition = std::make_unique<services::AcquisitionCoordinator>(
            context, *runtime->m_games, *hosts.sessions, hosts.metadata, hosts.coordinator);
    }
    runtime->m_launcher =
        std::make_unique<services::LaunchService>(context, runtime->m_acquisition.get());

    runtime->runMaintenance();

    if (auto interrupted = runtime->m_launcher->interruptedTransactions();
        interrupted && !interrupted.value().empty()) {
        logWarning(kComponent, "interrupted configuration transactions found",
                   {{"count", std::to_string(interrupted.value().size())}});
        runtime->m_notices.push_back(
            {LogLevel::Warning, "Interrupted configuration change",
             "A previous launch stopped while writing game configuration files. Use Recover "
             "next to Play to put the previous files back before launching again."});
    }
    return runtime;
}

AppRuntime::~AppRuntime() {
    shutdown();
    m_launcher.reset();
    m_acquisition.reset();
    m_sharing.reset();
    m_playsets.reset();
    m_games.reset();
    m_context.reset();
    m_transactions.reset();
    m_database.reset();
    if (m_ownsLogSink)
        setLogSink({});
}

void AppRuntime::runMaintenance() {
    const std::int64_t now = m_context->now();
    persistence::PlaysetRepository playsets(*m_database);
    if (auto purged =
            playsets.purgeHistoryOlderThan(now - kHistoryRetentionMs, kHistoryKeepPerPlayset);
        !purged) {
        logWarning(kComponent, "purging playset history failed",
                   {{"error", purged.error().message}});
    } else if (purged.value() > 0) {
        logInfo(kComponent, "purged old playset history",
                {{"entries", std::to_string(purged.value())}});
    }
    persistence::AcquisitionJobRepository jobs(*m_database);
    if (auto removed = jobs.removeFinishedOlderThan(now - kJobRetentionMs); !removed) {
        logWarning(kComponent, "removing finished acquisition jobs failed",
                   {{"error", removed.error().message}});
    } else if (removed.value() > 0) {
        logInfo(kComponent, "removed finished acquisition jobs",
                {{"jobs", std::to_string(removed.value())}});
    }
    if (Status pruned = m_transactions->prune(kKeptTransactions); !pruned)
        logWarning(kComponent, "pruning configuration transactions failed",
                   {{"error", pruned.error().message}});
}

std::filesystem::path AppRuntime::logFile() const {
    return m_logSink ? m_logSink->currentFile() : std::filesystem::path();
}

void AppRuntime::setBeforeProcessStart(std::function<void()> hook) {
    *m_beforeProcessStart = std::move(hook);
}

void AppRuntime::shutdown() {
    if (m_shutDown)
        return;
    m_shutDown = true;
    if (m_acquisition)
        m_acquisition->shutdown();
    logInfo(kComponent, "application runtime stopped");
}

} // namespace pmm::app
