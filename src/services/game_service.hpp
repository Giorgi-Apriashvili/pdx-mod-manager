#pragma once

// Game discovery, manual locations and the installed-mod inventory cache.
//
// For every catalog game with an injected adapter, a refresh uses the manually
// configured installation stored in the database when there is one (and
// re-validates it with inspectInstallation()), otherwise the first result of
// adapter.discover(). The installation is persisted, the mods are scanned and
// the inventory cache is replaced atomically. Problems are reported per game in
// GameState::scanError, never as a failure of the whole refresh.
//
// The inventory cache lets the application show the last known state at
// start-up (cachedStates()) before the first rescan finishes. A rescan drops
// cached local-mod fingerprints: editing a file inside a mod folder does not
// change the folder's modification time, so nothing cheap proves that a cached
// fingerprint still matches. ShareService::exportCode() recomputes them.

#include "core/model.hpp"
#include "core/result.hpp"
#include "services/service_context.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pmm::services {

struct GameState {
    GameKey key;
    std::string displayName;
    bool supported = false; // an adapter for this game is available
    std::optional<GameInstallation> installation;
    std::vector<InstalledMod> inventory; // ordered by source, then provider id
    // Discovery, validation or scan problem. When a scan fails the inventory is
    // the previously cached one.
    std::optional<Error> scanError;
    Capabilities capabilities; // of the installation; all false without one
};

class GameService {
public:
    explicit GameService(const ServiceContext& context) noexcept : m_ctx(context) {}

    // Every catalog game, supported ones first (catalog order).
    std::vector<GameState> refreshAll();
    // NotFound for unknown keys; DatabaseError when the cache cannot be written.
    Result<GameState> refreshGame(const GameKey& key);
    // Rescans the mods of the stored installation without rediscovering it
    // (used after a Workshop item finished installing).
    Result<GameState> rescanMods(const GameKey& key);

    // Validates and stores a manually selected installation, then scans it.
    // userDataPath may be empty for the OS default. Nothing is stored when the
    // adapter rejects the folders.
    Result<GameState> setManualLocation(const GameKey& key,
                                        const std::filesystem::path& installPath,
                                        const std::filesystem::path& userDataPath);
    // Forgets the manual location and rediscovers the game.
    Result<GameState> clearManualLocation(const GameKey& key);

    // Last known state from the database, without touching the filesystem.
    std::vector<GameState> cachedStates();
    Result<GameState> cachedState(const GameKey& key);
    Result<std::vector<InstalledMod>> cachedInventory(const GameKey& key);
    Result<std::optional<GameInstallation>> installation(const GameKey& key);

    // Creates a playset from the game's current mod configuration (for
    // example dlc_load.json) and makes it the active playset. The name
    // defaults to "Current game configuration" and is made unique. Returns
    // the new playset id.
    Result<std::int64_t> importCurrentConfiguration(const GameKey& key,
                                                    const std::string& name = {});

private:
    GameState baseState(const GameKey& key) const;
    Result<GameState> scanInto(GameState state, const games::GameAdapter& adapter,
                               const GameInstallation& installation);

    const ServiceContext& m_ctx;
};

} // namespace pmm::services
