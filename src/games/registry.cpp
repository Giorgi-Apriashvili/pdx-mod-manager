// Adapter registry and the real discovery environment.

#include "games/eu4.hpp"
#include "games/game_adapter.hpp"
#include "games/stellaris.hpp"
#include "platform/paths.hpp"
#include "platform/steam.hpp"

#include <array>

namespace pmm::games {

DiscoveryEnvironment DiscoveryEnvironment::current() {
    DiscoveryEnvironment env;
    const platform::StandardDirectories dirs = platform::standardDirectories();
    env.documentsDir = dirs.documents;
    env.home = dirs.home;
    if (auto root = platform::steam::findSteamRoot(); root)
        env.steamRoot = std::move(root).value();
    return env;
}

std::span<const std::unique_ptr<GameAdapter>> adapters() {
    // Catalog order.
    static const std::array<std::unique_ptr<GameAdapter>, 2> kAdapters{
        std::make_unique<Eu4Adapter>(), std::make_unique<StellarisAdapter>()};
    return kAdapters;
}

const GameAdapter* findAdapter(std::string_view gameKey) {
    for (const auto& adapter : adapters()) {
        if (adapter->key() == gameKey)
            return adapter.get();
    }
    return nullptr;
}

} // namespace pmm::games
