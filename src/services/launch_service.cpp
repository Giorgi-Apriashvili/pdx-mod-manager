#include "services/launch_service.hpp"

#include "core/log.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "persistence/installation_repository.hpp"
#include "persistence/inventory_repository.hpp"
#include "persistence/launch_history_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "platform/fs.hpp"
#include "services/acquisition_coordinator.hpp"
#include "services/internal/util.hpp"

#include <algorithm>
#include <set>
#include <system_error>
#include <utility>

namespace pmm::services {
namespace {

namespace fs = std::filesystem;

constexpr const char* kComponent = "services.launch";

struct Prepared {
    LaunchCheck check;
    const games::GameAdapter* adapter = nullptr;
    std::optional<Playset> playset;
    std::vector<InstalledMod> orderedEnabled; // set only when every enabled entry is Ready
};

std::string quoted(const std::string& text) {
    return "\"" + text + "\"";
}

std::string argumentList(const std::vector<std::string>& arguments) {
    std::string out = "[";
    for (std::size_t i = 0; i < arguments.size(); ++i)
        out += (i ? ", " : "") + quoted(arguments[i]);
    return out + "]";
}

// The folder itself when it exists, else its nearest existing ancestor (the
// folders a launch would have to create live there).
fs::path existingAncestor(fs::path path) {
    std::error_code ec;
    while (!path.empty() && !fs::exists(path, ec)) {
        const fs::path parent = path.parent_path();
        if (parent == path)
            break;
        path = parent;
    }
    return path;
}

void checkWritable(const fs::path& directory, const char* what, std::set<fs::path>& checked,
                   LaunchCheck& check) {
    if (directory.empty())
        return;
    const fs::path probe = existingAncestor(directory);
    if (probe.empty() || !checked.insert(probe).second)
        return;
    auto writable = platform::isDirectoryWritable(probe);
    if (!writable) {
        check.blockers.push_back(
            "The " + std::string(what) + " (" + platform::pathToUtf8(directory) +
            ") could not be checked for write access: " + writable.error().message);
    } else if (!writable.value()) {
        check.blockers.push_back("The " + std::string(what) + " (" +
                                 platform::pathToUtf8(directory) +
                                 ") is not writable, so the mod configuration cannot be written. "
                                 "Check the folder's permissions (or read-only flag) and retry.");
    }
}

Prepared prepare(const ServiceContext& ctx, AcquisitionCoordinator* coordinator,
                 const GameKey& game, std::int64_t playsetId, const LaunchCheckOptions& options) {
    Prepared prepared;
    LaunchCheck& check = prepared.check;
    const std::string gameName = gameDisplayName(game);
    prepared.adapter = ctx.adapterFor(game);
    if (!prepared.adapter) {
        check.blockers.push_back(gameName + " is not supported by this version, so it cannot be "
                                            "launched from here.");
        return prepared;
    }
    const games::GameAdapter& adapter = *prepared.adapter;

    persistence::InstallationRepository installations(ctx.database);
    auto installation = installations.get(game);
    if (!installation) {
        check.blockers.push_back("The application database could not be read: " +
                                 installation.error().message);
        return prepared;
    }
    if (!installation.value()) {
        check.blockers.push_back(gameName + " has not been found. Rescan, or locate the game "
                                            "folder.");
    } else {
        check.installation = *installation.value();
        const GameInstallation& install = *check.installation;
        std::error_code ec;
        if (install.executablePath.empty() || !fs::is_regular_file(install.executablePath, ec)) {
            check.blockers.push_back("The " + gameName + " executable was not found (" +
                                     platform::pathToUtf8(install.executablePath) +
                                     "). Verify the game files, or locate the game again.");
        }
        if (!adapter.capabilities(install).directLaunch)
            check.blockers.push_back("Direct launch is not available for this installation of " +
                                     gameName + ".");
    }

    persistence::PlaysetRepository playsets(ctx.database);
    auto playset = playsets.get(playsetId);
    if (!playset) {
        check.blockers.push_back(playset.error().code == ErrorCode::NotFound
                                     ? "The selected playset no longer exists. Choose a playset."
                                     : "The playset could not be read: " + playset.error().message);
    } else if (playset.value().game != game) {
        check.blockers.push_back("The selected playset belongs to " +
                                 gameDisplayName(playset.value().game) + ".");
    } else {
        prepared.playset = std::move(playset).value();
        for (PlaysetEntry& entry : prepared.playset->entries)
            entry.identity.game = game;
        persistence::InventoryRepository inventory(ctx.database);
        persistence::AcquisitionJobRepository jobRepo(ctx.database);
        auto mods = inventory.list(game);
        auto jobs = jobRepo.listForGame(game);
        if (!mods || !jobs) {
            check.blockers.push_back("The application database could not be read: " +
                                     (!mods ? mods.error().message : jobs.error().message));
        } else {
            std::vector<AcquisitionJob> allJobs = std::move(jobs).value();
            if (coordinator)
                allJobs = detail::overlayJobs(std::move(allJobs), coordinator->liveJobs(game));
            const InstalledModMap byIdentity = detail::toMap(mods.value());
            const std::vector<PlaysetEntry>& entries = prepared.playset->entries;
            const std::vector<EntryReadiness> readiness =
                computePlaysetReadiness(entries, byIdentity, allJobs);
            check.readiness = evaluateLaunch(entries, readiness);
            for (const LaunchBlocker& blocker : check.readiness.blockers)
                check.blockers.push_back(blocker.reason);
            if (check.readiness.enabledCount == 0)
                check.warnings.push_back("No mods are enabled: the game will start without mods.");
            if (check.readiness.unresolvedDisabledCount > 0)
                check.warnings.push_back(
                    std::to_string(check.readiness.unresolvedDisabledCount) + " disabled " +
                    (check.readiness.unresolvedDisabledCount == 1 ? "entry is" : "entries are") +
                    " not available; disabled entries do not block launching.");
            std::size_t withWarnings = 0;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].enabled && !readiness[i].warnings.empty())
                    ++withWarnings;
            }
            if (withWarnings > 0)
                check.warnings.push_back(std::to_string(withWarnings) + " enabled " +
                                         (withWarnings == 1 ? "mod has" : "mods have") +
                                         " warnings (versions or dependencies); see the mod list.");
            if (check.readiness.canLaunch) {
                for (const PlaysetEntry& entry : entries) {
                    if (entry.enabled)
                        prepared.orderedEnabled.push_back(byIdentity.at(entry.identity));
                }
            }
        }
    }

    if (check.installation) {
        const GameInstallation& install = *check.installation;
        std::set<fs::path> checked;
        if (install.userDataPath.empty()) {
            check.blockers.push_back("The " + gameName +
                                     " user data folder is unknown. Locate the game again.");
        } else if (options.probeWriteAccess) {
            checkWritable(install.userDataPath, "user data folder", checked, check);
            checkWritable(install.userDataPath / "mod", "mod folder", checked, check);
        }

        const games::ProcessNames names = adapter.processNames(install);
        if (!ctx.listProcesses) {
            check.warnings.push_back("Running instances of " + gameName + " could not be checked.");
        } else if (auto processes = ctx.listProcesses(); !processes) {
            check.warnings.push_back("Could not check whether " + gameName +
                                     " is already running: " + processes.error().message);
        } else {
            std::set<std::string> reported;
            for (const platform::RunningProcess& process : processes.value()) {
                for (const std::string& name : names.game) {
                    if (platform::executableNameMatches(process.executableName, name) &&
                        reported.insert(name).second) {
                        check.runningInstance = true;
                        check.blockers.push_back(gameName + " is already running (" +
                                                 process.executableName + ", process " +
                                                 std::to_string(process.pid) +
                                                 "). Close it before launching.");
                    }
                }
                for (const std::string& name : names.conflictingWriters) {
                    if (platform::executableNameMatches(process.executableName, name) &&
                        reported.insert(name).second) {
                        check.runningInstance = true;
                        check.blockers.push_back(
                            process.executableName +
                            " (Paradox launcher) is running and may rewrite the mod "
                            "configuration. Close it before launching.");
                    }
                }
            }
        }
    }

    auto incomplete = ctx.transactions.listIncomplete();
    if (!incomplete) {
        check.warnings.push_back("Interrupted configuration changes could not be checked: " +
                                 incomplete.error().message);
    } else if (!incomplete.value().empty()) {
        check.interruptedTransaction = true;
        check.blockers.push_back("A previous configuration change was interrupted (" +
                                 incomplete.value().front().label +
                                 "). Recover it before launching so that the game's files are "
                                 "consistent.");
    }
    check.canLaunch = check.blockers.empty();
    return prepared;
}

std::int64_t recordLaunch(const ServiceContext& ctx, const GameKey& game, std::int64_t playsetId,
                          std::optional<std::int64_t> pid, std::string outcome, std::string detail,
                          std::string transactionId) {
    persistence::LaunchHistoryRepository history(ctx.database);
    persistence::LaunchRecord record;
    record.game = game;
    record.playsetId = playsetId;
    record.startedAt = ctx.now();
    record.pid = pid;
    record.outcome = std::move(outcome);
    record.detail = std::move(detail);
    record.transactionId = std::move(transactionId);
    auto id = history.record(record);
    if (!id) {
        logWarning(kComponent, "recording the launch failed", {{"error", id.error().message}});
        return 0;
    }
    return id.value();
}

Status validatePlan(const LaunchPlan& plan, const GameKey& game) {
    std::error_code ec;
    if (!plan.game.empty() && plan.game != game)
        return makeError(ErrorCode::Internal, "The launch plan was built for another game.");
    if (!plan.executable.is_absolute() || !fs::is_regular_file(plan.executable, ec))
        return makeError(ErrorCode::ValidationFailed,
                         "The game executable in the launch plan does not exist.",
                         platform::pathToUtf8(plan.executable));
    for (const ConfigWrite& write : plan.configWrites) {
        if (!write.target.is_absolute())
            return makeError(ErrorCode::Internal,
                             "The launch plan contains a relative configuration path.",
                             platform::pathToUtf8(write.target));
    }
    return {};
}

} // namespace

std::string LaunchService::transactionLabel(const GameKey& game, std::int64_t playsetId,
                                            std::string_view playsetName) {
    return "launch " + game + " playset " + std::to_string(playsetId) + " (" +
           std::string(playsetName) + ")";
}

LaunchCheck LaunchService::check(const GameKey& game, std::int64_t playsetId,
                                 const LaunchCheckOptions& options) {
    return prepare(m_ctx, m_coordinator, game, playsetId, options).check;
}

Result<LaunchOutcome> LaunchService::launch(const GameKey& game, std::int64_t playsetId) {
    const std::string gameName = gameDisplayName(game);
    Prepared prepared = prepare(m_ctx, m_coordinator, game, playsetId, LaunchCheckOptions{});
    const LaunchCheck& check = prepared.check;
    if (!check.canLaunch) {
        std::string detail;
        for (const std::string& blocker : check.blockers)
            detail += (detail.empty() ? "" : "\n") + blocker;
        const ErrorCode code = check.interruptedTransaction ? ErrorCode::Interrupted
                               : check.runningInstance      ? ErrorCode::ConflictingProcess
                                                            : ErrorCode::ValidationFailed;
        return makeError(code, gameName + " cannot be launched yet: " + check.blockers.front(),
                         detail);
    }

    // Steam treats an open helper session as the game running.
    if (m_coordinator)
        m_coordinator->closeSessionsForLaunch(game);

    const GameInstallation& installation = *check.installation;
    auto plan = prepared.adapter->buildLaunchPlan(installation, prepared.orderedEnabled);
    if (!plan) {
        recordLaunch(m_ctx, game, playsetId, std::nullopt,
                     std::string(errorCodeName(plan.error().code)), plan.error().message, {});
        return Error(plan.error().code, "Preparing the launch failed. " + plan.error().message,
                     plan.error().detail);
    }
    if (Status valid = validatePlan(plan.value(), game); !valid) {
        recordLaunch(m_ctx, game, playsetId, std::nullopt,
                     std::string(errorCodeName(valid.error().code)), valid.error().message, {});
        return valid.error();
    }

    std::string transactionId;
    if (!plan.value().configWrites.empty()) {
        std::vector<platform::FileWrite> writes;
        for (const ConfigWrite& write : plan.value().configWrites)
            writes.push_back(platform::FileWrite{write.target, write.contents});
        auto applied = m_ctx.transactions.apply(
            transactionLabel(game, playsetId, prepared.playset->name), std::move(writes));
        if (!applied) {
            recordLaunch(m_ctx, game, playsetId, std::nullopt,
                         std::string(errorCodeName(applied.error().code)), applied.error().message,
                         {});
            // A failed rollback leaves the journal incomplete for recover().
            auto incomplete = m_ctx.transactions.listIncomplete();
            const bool undone = incomplete && incomplete.value().empty();
            return Error(applied.error().code,
                         std::string("Writing the game configuration failed, so the game was not "
                                     "started; ") +
                             (undone ? "the previous configuration is unchanged. "
                                     : "the change could not be fully undone. Recover the "
                                       "interrupted configuration change before launching. ") +
                             applied.error().message,
                         applied.error().detail);
        }
        transactionId = applied.value().id;
    }

    platform::ProcessSpec spec;
    spec.executable = plan.value().executable;
    spec.workingDirectory = plan.value().workingDirectory;
    spec.arguments = plan.value().arguments;
    spec.environment = plan.value().environment;
    Result<platform::StartedProcess> started =
        m_ctx.startProcess
            ? m_ctx.startProcess(spec)
            : Result<platform::StartedProcess>(
                  makeError(ErrorCode::ProcessStartFailed, "No process starter is configured."));
    if (!started) {
        std::string detail = "executable: " + platform::pathToUtf8(spec.executable) +
                             "\nworking directory: " + platform::pathToUtf8(spec.workingDirectory) +
                             "\narguments: " + argumentList(spec.arguments) + "\nenvironment:";
        for (const auto& [name, value] : spec.environment)
            detail += " " + name + "=" + value;
        detail += "\nerror: " + started.error().message;
        if (!started.error().detail.empty())
            detail += " (" + started.error().detail + ")";
        std::string message = gameName + " could not be started. ";
        if (!transactionId.empty()) {
            auto restored = m_ctx.transactions.restore(transactionId);
            if (restored) {
                message += "The previous game configuration was restored. ";
            } else {
                message += "Restoring the previous game configuration failed; use \"Restore "
                           "pre-launch configuration\". ";
                detail += "\nrestore: " + restored.error().message;
            }
        }
        message += started.error().message;
        recordLaunch(m_ctx, game, playsetId, std::nullopt,
                     std::string(errorCodeName(ErrorCode::ProcessStartFailed)), detail,
                     transactionId);
        logError(kComponent, "game process start failed", {{"game", game}, {"detail", detail}});
        return makeError(ErrorCode::ProcessStartFailed, std::move(message), std::move(detail));
    }

    LaunchOutcome outcome;
    outcome.pid = started.value().pid;
    outcome.transactionId = transactionId;
    outcome.warnings = check.warnings;
    for (const std::string& warning : plan.value().warnings)
        outcome.warnings.push_back(warning);
    outcome.historyId = recordLaunch(m_ctx, game, playsetId, outcome.pid, "started",
                                     platform::pathToUtf8(spec.executable), transactionId);
    outcome.plan = std::move(plan).value();
    if (Status pruned = m_ctx.transactions.prune(kKeptTransactions); !pruned)
        logWarning(kComponent, "pruning old transactions failed",
                   {{"error", pruned.error().message}});
    logInfo(kComponent, "game started",
            {{"game", game}, {"pid", std::to_string(outcome.pid)}, {"transaction", transactionId}});
    return outcome;
}

Result<std::vector<platform::TransactionRecord>> LaunchService::interruptedTransactions() {
    return m_ctx.transactions.listIncomplete();
}

Result<platform::TransactionRecord>
LaunchService::recoverInterrupted(std::string_view transactionId) {
    return m_ctx.transactions.recover(transactionId);
}

Result<std::optional<platform::TransactionRecord>>
LaunchService::lastRestorableTransaction(const GameKey& game) {
    auto committed = m_ctx.transactions.listCommitted();
    if (!committed)
        return std::move(committed).error();
    const std::string prefix = "launch " + game + " playset ";
    for (auto it = committed.value().rbegin(); it != committed.value().rend(); ++it) {
        if (it->label.rfind(prefix, 0) == 0)
            return std::optional<platform::TransactionRecord>(*it);
    }
    return std::optional<platform::TransactionRecord>();
}

Result<platform::TransactionRecord>
LaunchService::restoreTransaction(std::string_view transactionId) {
    return m_ctx.transactions.restore(transactionId);
}

} // namespace pmm::services
