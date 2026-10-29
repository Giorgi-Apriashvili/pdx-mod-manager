#pragma once

// Cache of the mods found on disk by the most recent scan of each game. It is
// independent from playset membership: replacing a game's inventory never
// touches playsets, and a mod can be installed without being in any playset.
//
// InstalledMod::contentUpdatedAt (unix seconds) is stored as milliseconds like
// every other timestamp and converted back on read.

#include "core/model.hpp"
#include "core/result.hpp"
#include "persistence/database.hpp"

#include <optional>
#include <vector>

namespace pmm::persistence {

class InventoryRepository {
public:
    explicit InventoryRepository(Database& db) noexcept : m_db(db) {}

    // Atomically replaces every cached mod of `game` with `mods`. Every mod
    // must belong to `game` and identities must be unique; otherwise nothing
    // is changed and InvalidArgument is returned.
    Status replaceForGame(const GameKey& game, const std::vector<InstalledMod>& mods);
    // Ordered by source, then provider id.
    Result<std::vector<InstalledMod>> list(const GameKey& game);
    // nullopt when the identity is not installed.
    Result<std::optional<InstalledMod>> find(const ModIdentity& identity);

private:
    Database& m_db;
};

} // namespace pmm::persistence
