#pragma once

// Dependencies shared by every service, injected by the composition root (the
// Qt application or the CLI) and by tests. Services never reach for globals:
// they do not use the adapter registry, the real discovery environment, the
// system clock or OS process APIs unless the composition root wires them in
// (useSystemDefaults() does that for the clock, processes and fingerprints).
//
// Threading model: the application owns one ServiceContext and calls every
// service from ONE dedicated worker thread. Services are synchronous and not
// thread-safe per instance. They never block on the network or the Steam
// helper: those arrive as events (see acquisition_coordinator.hpp). The
// Database itself is thread-safe, but the GUI thread should still go through
// the worker so that it never observes a half-finished service operation.

#include "core/model.hpp"
#include "core/result.hpp"
#include "games/game_adapter.hpp"
#include "persistence/database.hpp"
#include "platform/file_transaction.hpp"
#include "platform/process.hpp"
#include "services/app_paths.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::services {

using Clock = std::function<std::int64_t()>; // unix milliseconds
using ProcessStarter =
    std::function<Result<platform::StartedProcess>(const platform::ProcessSpec&)>;
using ProcessLister = std::function<Result<std::vector<platform::RunningProcess>>()>;
// Content fingerprint of a local mod folder ("sha256:<hex>").
using Fingerprinter = std::function<Result<std::string>(const std::filesystem::path&)>;

struct ServiceContext {
    ServiceContext(persistence::Database& db, platform::FileTransactionManager& tx) noexcept
        : database(db), transactions(tx) {}

    persistence::Database& database;
    platform::FileTransactionManager& transactions;  // journal root: AppPaths::transactionsDir
    std::vector<const games::GameAdapter*> adapters; // not owned; must outlive the services
    games::DiscoveryEnvironment environment;
    AppPaths paths;
    Clock clock;                 // unset: nowUnixMs()
    ProcessStarter startProcess; // unset: launching fails with an explanation
    ProcessLister listProcesses; // unset: running-instance checks become warnings
    Fingerprinter fingerprint;   // unset: exports carry no local fingerprints

    // nullptr when no adapter for `gameKey` was injected.
    const games::GameAdapter* adapterFor(std::string_view gameKey) const noexcept;
    std::int64_t now() const;
};

// Wires the production implementations: nowUnixMs, platform::startDetached,
// platform::listProcesses and providers::fingerprintDirectory. Adapters,
// environment and paths stay the caller's responsibility.
void useSystemDefaults(ServiceContext& context);

// Keys in the settings table owned by the services layer.
namespace settings_key {
inline constexpr const char* LastGame = "app.lastGameKey";
// "game.<key>.activePlaysetId"
std::string activePlayset(const GameKey& game);
} // namespace settings_key

// Display name from the catalog, or the key itself for unknown games.
std::string gameDisplayName(const GameKey& game);

} // namespace pmm::services
