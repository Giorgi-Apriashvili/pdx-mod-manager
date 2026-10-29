#pragma once

// Validated direct launch with the active playset (spec section 7).
//
// check() gathers every blocking condition and warning without changing
// anything: installation present and its executable on disk, direct launch
// available, the playset belongs to the game, every ENABLED entry Ready
// (disabled entries never block), the user-data and mod folders writable, no
// running instance of the game or conflicting configuration writer (Paradox
// launcher), and no interrupted configuration transaction awaiting recovery.
//
// launch() re-checks, builds the adapter's LaunchPlan for the enabled mods in
// playset order and validates it. Only then does it ask the coordinator to
// close the game's Steam helper session and park its acquisition jobs, so a
// launch refused while preparing leaves acquisition alone. It applies
// plan.configWrites as ONE file transaction (backups, rollback, crash journal;
// label "launch <game> playset <id> (<name>)") and starts the executable
// through the injected process starter with the plan's argv vector, working
// folder and environment (never a shell). If the process cannot be started,
// the transaction's backups are restored so the previous configuration is
// back, and ProcessStartFailed is returned with diagnostics. When no game was
// started after the session was closed, the parked jobs are resumed.
// Every attempt is recorded in the launch history.
//
// The configuration written for a launch stays in effect until the next
// launch or until the user restores it (restoreTransaction()); the official
// launcher simply overwrites it the next time it is used.
//
// Not thread-safe; see service_context.hpp.

#include "core/model.hpp"
#include "core/readiness.hpp"
#include "core/result.hpp"
#include "platform/file_transaction.hpp"
#include "services/acquisition_coordinator.hpp"
#include "services/service_context.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::services {

struct LaunchCheck {
    bool canLaunch = false;
    std::vector<std::string> blockers; // shown next to Play
    std::vector<std::string> warnings;
    std::optional<GameInstallation> installation;
    LaunchReadiness readiness; // per-entry verdict (enabled entries only)
    bool interruptedTransaction = false;
    bool runningInstance = false; // game or conflicting writer running
};

struct LaunchOutcome {
    std::int64_t pid = 0;
    std::string transactionId; // empty when the plan had no configuration writes
    LaunchPlan plan;
    std::vector<std::string> warnings;
    std::int64_t historyId = 0;
    // What parking the game's acquisition jobs for the launch changed; report
    // it so that job views refresh.
    CoordinatorUpdate acquisition;
};

class LaunchService {
public:
    // `coordinator` may be null (no Workshop sessions to close, no live jobs).
    LaunchService(const ServiceContext& context, AcquisitionCoordinator* coordinator) noexcept
        : m_ctx(context), m_coordinator(coordinator) {}

    LaunchCheck check(const GameKey& game, std::int64_t playsetId);
    Result<LaunchOutcome> launch(const GameKey& game, std::int64_t playsetId);

    // Configuration transactions interrupted by a crash (any game).
    Result<std::vector<platform::TransactionRecord>> interruptedTransactions();
    Result<platform::TransactionRecord> recoverInterrupted(std::string_view transactionId);
    // Newest committed launch transaction of `game` (the configuration from
    // before that launch can be restored).
    Result<std::optional<platform::TransactionRecord>>
    lastRestorableTransaction(const GameKey& game);
    // User-initiated "restore pre-launch configuration".
    Result<platform::TransactionRecord> restoreTransaction(std::string_view transactionId);

    // "launch <game> playset <id> (<name>)"
    static std::string transactionLabel(const GameKey& game, std::int64_t playsetId,
                                        std::string_view playsetName);
    // Finished transactions kept for restore (older ones are pruned after launches).
    static constexpr std::size_t kKeptTransactions = 20;

private:
    const ServiceContext& m_ctx;
    AcquisitionCoordinator* m_coordinator;
};

} // namespace pmm::services
