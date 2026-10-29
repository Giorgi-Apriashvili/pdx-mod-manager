#pragma once

// Static catalog of games this application knows about. A game being listed here
// does not mean it is supported: only entries with `adapterAvailable == true`
// have an implemented, tested adapter. Share codes for other games are rejected
// with an actionable "not supported by this version" message.

#include <span>
#include <string_view>

namespace pmm {

struct GameCatalogEntry {
    std::string_view key;              // stable protocol key, e.g. "stellaris"
    std::string_view displayName;      // "Stellaris"
    std::string_view steamAppId;       // "281990"
    std::string_view userDataFolder;   // folder under "Paradox Interactive"
    std::span<const std::string_view> sources; // provider keys valid for this game
    bool adapterAvailable;             // implemented in this release
};

// All known games, supported ones first.
std::span<const GameCatalogEntry> gameCatalog() noexcept;

// nullptr when the key is unknown.
const GameCatalogEntry* findGame(std::string_view key) noexcept;

bool isSourceValidForGame(std::string_view gameKey, std::string_view sourceKey) noexcept;

} // namespace pmm
