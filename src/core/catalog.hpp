#pragma once

// Static catalog of games this application knows about. A game being listed here
// does not mean it is supported: only entries with `adapterAvailable == true`
// have an implemented, tested adapter. Share codes for other games (none at
// present) are rejected with an actionable "not supported by this version"
// message.

#include <span>
#include <string_view>

namespace pmm {

// What a mod's dependencies (the descriptor's `dependencies`; Victoria 3: the
// metadata's relationships of rel_type "dependency") mean in a game, and so
// what core/dependencies.hpp reports about them.
enum class DependencyMeaning {
    // The named mods are needed: each one that is not installed, not in the
    // playset, disabled, or enabled but later in the list than the mod that
    // names it is reported, and the application warns about it.
    Required,
    // The named mods are not needed, but the list order decides which mod
    // wins a shared file, and the mod names them to load before it: only a
    // named mod that is enabled and comes later in the list than the mod that
    // names it is reported (an order hint). A missing, disabled or absent one
    // is not.
    LoadAfterInList,
    // The named mods are not needed, and their place in the list is not
    // checked either: nothing is reported, and the details only list them.
    LoadAfter,
};

struct GameCatalogEntry {
    std::string_view key;                      // stable protocol key, e.g. "stellaris"
    std::string_view displayName;              // "Stellaris"
    std::string_view steamAppId;               // "281990"
    std::string_view userDataFolder;           // folder under "Paradox Interactive"
    std::span<const std::string_view> sources; // provider keys valid for this game
    bool adapterAvailable;                     // implemented in this release
    DependencyMeaning dependencyMeaning = DependencyMeaning::Required;
    // The game's own documentation says that it loads a mod after the mods
    // its dependencies name (Hearts of Iron IV's wiki). Only how the
    // application words the list depends on it.
    bool dependencyLoadAfterDocumented = false;
};

// All known games, supported ones first.
std::span<const GameCatalogEntry> gameCatalog() noexcept;

// nullptr when the key is unknown.
const GameCatalogEntry* findGame(std::string_view key) noexcept;

bool isSourceValidForGame(std::string_view gameKey, std::string_view sourceKey) noexcept;

// The game's dependencyMeaning; Required for an unknown key.
DependencyMeaning dependencyMeaningOf(std::string_view gameKey) noexcept;

} // namespace pmm
