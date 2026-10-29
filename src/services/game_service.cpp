#include "services/game_service.hpp"

#include "core/catalog.hpp"
#include "core/import_plan.hpp"
#include "core/log.hpp"
#include "persistence/installation_repository.hpp"
#include "persistence/inventory_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "persistence/settings_repository.hpp"
#include "platform/fs.hpp"
#include "services/internal/util.hpp"

#include <map>
#include <set>
#include <utility>

namespace pmm::services {
namespace {

constexpr const char* kComponent = "services.games";
constexpr const char* kDefaultConfigurationName = "Current game configuration";

// Scoped to `game`, unique identities (first occurrence wins), sorted.
std::vector<InstalledMod> normalizeInventory(const GameKey& game, std::vector<InstalledMod> mods) {
    std::vector<InstalledMod> out;
    out.reserve(mods.size());
    std::set<ModIdentity> seen;
    for (InstalledMod& mod : mods) {
        if (mod.identity.game.empty())
            mod.identity.game = game;
        if (mod.identity.game != game) {
            logWarning(kComponent, "scan returned a mod of another game; ignored",
                       {{"game", game}, {"mod", toString(mod.identity)}});
            continue;
        }
        if (!seen.insert(mod.identity).second) {
            logWarning(kComponent, "scan returned a duplicate identity; kept the first",
                       {{"mod", toString(mod.identity)}});
            continue;
        }
        out.push_back(std::move(mod));
    }
    detail::sortInventory(out);
    return out;
}

// Keeps fingerprints computed earlier when the content looks unchanged.
void carryOverFingerprints(std::vector<InstalledMod>& fresh,
                           const std::vector<InstalledMod>& cached) {
    std::map<ModIdentity, const InstalledMod*> byIdentity;
    for (const InstalledMod& mod : cached)
        byIdentity.emplace(mod.identity, &mod);
    for (InstalledMod& mod : fresh) {
        if (!mod.fingerprint.empty())
            continue;
        const auto it = byIdentity.find(mod.identity);
        if (it == byIdentity.end())
            continue;
        const InstalledMod& old = *it->second;
        if (!old.fingerprint.empty() && old.contentPath == mod.contentPath &&
            old.contentUpdatedAt != 0 && old.contentUpdatedAt == mod.contentUpdatedAt)
            mod.fingerprint = old.fingerprint;
    }
}

Error withContext(const Error& error, const std::string& prefix) {
    return Error(error.code, prefix + error.message, error.detail);
}

} // namespace

GameState GameService::baseState(const GameKey& key) const {
    GameState state;
    state.key = key;
    state.displayName = gameDisplayName(key);
    const GameCatalogEntry* entry = findGame(key);
    state.supported = entry && entry->adapterAvailable && m_ctx.adapterFor(key) != nullptr;
    return state;
}

std::vector<GameState> GameService::refreshAll() {
    std::vector<GameState> states;
    for (const GameCatalogEntry& entry : gameCatalog()) {
        const GameKey key(entry.key);
        auto refreshed = refreshGame(key);
        if (refreshed) {
            states.push_back(std::move(refreshed).value());
            continue;
        }
        GameState state = baseState(key);
        state.scanError = refreshed.error();
        states.push_back(std::move(state));
    }
    return states;
}

Result<GameState> GameService::refreshGame(const GameKey& key) {
    if (!findGame(key))
        return makeError(ErrorCode::NotFound, "\"" + key + "\" is not a known game.");
    GameState state = baseState(key);
    const games::GameAdapter* adapter = m_ctx.adapterFor(key);
    if (!state.supported || !adapter)
        return state;

    persistence::InstallationRepository installations(m_ctx.database);
    persistence::InventoryRepository inventory(m_ctx.database);
    auto stored = installations.get(key);
    if (!stored)
        return std::move(stored).error();

    std::optional<GameInstallation> installation;
    const bool manual = stored.value() && stored.value()->manuallyConfigured;
    if (manual) {
        const GameInstallation& configured = *stored.value();
        auto inspected = adapter->inspectInstallation(configured.installPath,
                                                      configured.userDataPath, m_ctx.environment);
        if (inspected) {
            installation = std::move(inspected).value();
            installation->manuallyConfigured = true;
        } else {
            state.scanError = withContext(
                inspected.error(), "The manually selected " + state.displayName +
                                       " folder is no longer valid. Locate the game again or "
                                       "clear the manual location. ");
        }
    } else {
        std::vector<GameInstallation> found = adapter->discover(m_ctx.environment);
        if (!found.empty()) {
            installation = std::move(found.front());
            installation->manuallyConfigured = false;
        }
    }

    if (!installation) {
        // Not installed (or the manual folder is gone): drop the stale
        // detected installation and the inventory that belonged to it.
        if (stored.value() && !manual)
            PMM_TRY(installations.remove(key));
        PMM_TRY(inventory.replaceForGame(key, {}));
        return state;
    }
    installation->game = key;
    PMM_TRY(installations.upsert(*installation));
    return scanInto(std::move(state), *adapter, *installation);
}

Result<GameState> GameService::scanInto(GameState state, const games::GameAdapter& adapter,
                                        const GameInstallation& installation) {
    persistence::InventoryRepository inventory(m_ctx.database);
    state.installation = installation;
    state.capabilities = adapter.capabilities(installation);

    auto cached = inventory.list(state.key);
    if (!cached)
        return std::move(cached).error();
    auto scanned = adapter.scanMods(installation);
    if (!scanned) {
        state.scanError =
            withContext(scanned.error(), "Scanning the mods of " + state.displayName + " failed. ");
        state.inventory = std::move(cached).value();
        logWarning(kComponent, "mod scan failed",
                   {{"game", state.key}, {"error", scanned.error().message}});
        return state;
    }
    std::vector<InstalledMod> mods = normalizeInventory(state.key, std::move(scanned).value());
    carryOverFingerprints(mods, cached.value());
    PMM_TRY(inventory.replaceForGame(state.key, mods));
    state.inventory = std::move(mods);
    logInfo(kComponent, "mods scanned",
            {{"game", state.key}, {"count", std::to_string(state.inventory.size())}});
    return state;
}

Result<GameState> GameService::rescanMods(const GameKey& key) {
    const games::GameAdapter* adapter = m_ctx.adapterFor(key);
    auto stored = installation(key);
    if (!stored)
        return std::move(stored).error();
    if (!adapter || !stored.value())
        return refreshGame(key);
    return scanInto(baseState(key), *adapter, *stored.value());
}

Result<GameState> GameService::setManualLocation(const GameKey& key,
                                                 const std::filesystem::path& installPath,
                                                 const std::filesystem::path& userDataPath) {
    const games::GameAdapter* adapter = m_ctx.adapterFor(key);
    if (!adapter)
        return makeError(ErrorCode::UnsupportedGame,
                         gameDisplayName(key) + " is not supported by this version.");
    if (installPath.empty())
        return makeError(ErrorCode::InvalidArgument, "Choose the game's installation folder.");
    auto inspected = adapter->inspectInstallation(installPath, userDataPath, m_ctx.environment);
    if (!inspected)
        return std::move(inspected).error();
    GameInstallation installation = std::move(inspected).value();
    installation.game = key;
    installation.manuallyConfigured = true;
    persistence::InstallationRepository installations(m_ctx.database);
    PMM_TRY(installations.upsert(installation));
    logInfo(kComponent, "manual location set",
            {{"game", key}, {"install", platform::pathToUtf8(installation.installPath)}});
    return scanInto(baseState(key), *adapter, installation);
}

Result<GameState> GameService::clearManualLocation(const GameKey& key) {
    persistence::InstallationRepository installations(m_ctx.database);
    auto stored = installations.get(key);
    if (!stored)
        return std::move(stored).error();
    if (stored.value() && stored.value()->manuallyConfigured)
        PMM_TRY(installations.remove(key));
    return refreshGame(key);
}

std::vector<GameState> GameService::cachedStates() {
    std::vector<GameState> states;
    for (const GameCatalogEntry& entry : gameCatalog()) {
        auto state = cachedState(GameKey(entry.key));
        if (state) {
            states.push_back(std::move(state).value());
        } else {
            GameState failed = baseState(GameKey(entry.key));
            failed.scanError = state.error();
            states.push_back(std::move(failed));
        }
    }
    return states;
}

Result<GameState> GameService::cachedState(const GameKey& key) {
    if (!findGame(key))
        return makeError(ErrorCode::NotFound, "\"" + key + "\" is not a known game.");
    GameState state = baseState(key);
    if (!state.supported)
        return state;
    auto stored = installation(key);
    if (!stored)
        return std::move(stored).error();
    state.installation = std::move(stored).value();
    if (state.installation) {
        if (const games::GameAdapter* adapter = m_ctx.adapterFor(key))
            state.capabilities = adapter->capabilities(*state.installation);
    }
    auto mods = cachedInventory(key);
    if (!mods)
        return std::move(mods).error();
    state.inventory = std::move(mods).value();
    return state;
}

Result<std::vector<InstalledMod>> GameService::cachedInventory(const GameKey& key) {
    persistence::InventoryRepository inventory(m_ctx.database);
    return inventory.list(key);
}

Result<std::optional<GameInstallation>> GameService::installation(const GameKey& key) {
    persistence::InstallationRepository installations(m_ctx.database);
    return installations.get(key);
}

Result<std::int64_t> GameService::importCurrentConfiguration(const GameKey& key,
                                                             const std::string& name) {
    const games::GameAdapter* adapter = m_ctx.adapterFor(key);
    if (!adapter)
        return makeError(ErrorCode::UnsupportedGame,
                         gameDisplayName(key) + " is not supported by this version.");
    auto stored = installation(key);
    if (!stored)
        return std::move(stored).error();
    if (!stored.value())
        return makeError(ErrorCode::NotFound, gameDisplayName(key) +
                                                  " has not been found. Rescan, or locate the "
                                                  "game folder first.");
    auto mods = cachedInventory(key);
    if (!mods)
        return std::move(mods).error();
    auto current = adapter->readCurrentConfiguration(*stored.value(), mods.value());
    if (!current)
        return withContext(current.error(), "Reading the current mod configuration of " +
                                                gameDisplayName(key) + " failed. ");

    const InstalledModMap byIdentity = detail::toMap(mods.value());
    std::vector<PlaysetEntry> entries;
    std::set<ModIdentity> seen;
    for (PlaysetEntry entry : current.value()) {
        entry.identity.game = key;
        if (!seen.insert(entry.identity).second)
            continue; // listed twice in the game's configuration
        if (entry.nameHint.empty()) {
            const auto it = byIdentity.find(entry.identity);
            if (it != byIdentity.end())
                entry.nameHint = it->second.descriptor.name;
        }
        entries.push_back(std::move(entry));
    }

    persistence::PlaysetRepository playsets(m_ctx.database);
    auto existing = playsets.listForGame(key);
    if (!existing)
        return std::move(existing).error();
    const std::string requested = detail::trimmed(name);
    Playset playset;
    playset.game = key;
    playset.name = uniquePlaysetName(requested.empty() ? kDefaultConfigurationName : requested,
                                     detail::names(existing.value()));
    playset.entries = std::move(entries);

    std::int64_t id = 0;
    PMM_TRY(m_ctx.database.transaction([&]() -> Status {
        persistence::SettingsRepository settings(m_ctx.database);
        auto created = playsets.create(playset);
        if (!created)
            return std::move(created).error();
        id = created.value();
        PMM_TRY(settings.setInt(settings_key::activePlayset(key), id));
        return settings.set(settings_key::LastGame, key);
    }));
    return id;
}

} // namespace pmm::services
