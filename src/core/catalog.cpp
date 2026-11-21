#include "core/catalog.hpp"

#include "core/catalog_testing.hpp"
#include "core/model.hpp"

#include <array>
#include <cstddef>

namespace pmm {
namespace {

constexpr std::array<std::string_view, 2> kWorkshopAndLocal{source::SteamWorkshop, source::Local};

constexpr DependencyMeaning kRequired = DependencyMeaning::Required;
// Hearts of Iron IV: the Paradox-hosted HOI4 wiki (Modding, its 2023 and 2026
// captures) says `dependencies = { "Exact Name" }` makes a mod load after the
// named ones without making them required; a 2018 developer diary has a mod
// that needs a certain load order name the others there; hoi4.exe only logs
// "%s: Couldn't find dependency %s" (docs/load-order.md, Hearts of Iron IV).
// Popular mods list total conversions they do not need.
constexpr DependencyMeaning kLoadAfter = DependencyMeaning::LoadAfter;

// Supported games in alphabetical order of their keys, then the others. With no
// game remembered, the application opens the first installed supported game.
constexpr std::array<GameCatalogEntry, 5> kCatalog{{
    {"ck3", "Crusader Kings III", "1158310", "Crusader Kings III", kWorkshopAndLocal, true,
     kRequired},
    {"eu4", "Europa Universalis IV", "236850", "Europa Universalis IV", kWorkshopAndLocal, true,
     kRequired},
    {"hoi4", "Hearts of Iron IV", "394360", "Hearts of Iron IV", kWorkshopAndLocal, true,
     kLoadAfter},
    {"stellaris", "Stellaris", "281990", "Stellaris", kWorkshopAndLocal, true, kRequired},
    {"vic3", "Victoria 3", "529340", "Victoria 3", kWorkshopAndLocal, true, kRequired},
}};

// The catalog in use: kCatalog, except while a testing::ScopedGameCatalog
// replaces it. constinit keeps gameCatalog() usable during the static
// initialisation of other files.
constinit std::span<const GameCatalogEntry> g_catalog = kCatalog;

// testing::catalogWithUnsupportedGame(): kCatalog, then a known game without
// an adapter.
constexpr auto kCatalogWithUnsupportedGame = [] {
    std::array<GameCatalogEntry, kCatalog.size() + 1> entries{};
    for (std::size_t i = 0; i < kCatalog.size(); ++i)
        entries[i] = kCatalog[i];
    GameCatalogEntry& unsupported = entries.back();
    unsupported.key = testing::kUnsupportedGameKey;
    unsupported.displayName = "Example Unsupported Game";
    unsupported.steamAppId = "1";
    unsupported.userDataFolder = "Example Unsupported Game";
    unsupported.sources = kWorkshopAndLocal;
    unsupported.adapterAvailable = false;
    unsupported.dependencyMeaning = kRequired;
    return entries;
}();

} // namespace

std::span<const GameCatalogEntry> gameCatalog() noexcept {
    return g_catalog;
}

const GameCatalogEntry* findGame(std::string_view key) noexcept {
    for (const auto& entry : g_catalog) {
        if (entry.key == key)
            return &entry;
    }
    return nullptr;
}

bool isSourceValidForGame(std::string_view gameKey, std::string_view sourceKey) noexcept {
    const auto* game = findGame(gameKey);
    if (!game)
        return false;
    for (auto s : game->sources) {
        if (s == sourceKey)
            return true;
    }
    return false;
}

DependencyMeaning dependencyMeaningOf(std::string_view gameKey) noexcept {
    const auto* game = findGame(gameKey);
    return game ? game->dependencyMeaning : DependencyMeaning::Required;
}

namespace testing {

ScopedGameCatalog::ScopedGameCatalog(std::span<const GameCatalogEntry> entries) noexcept
    : m_previous(g_catalog) {
    g_catalog = entries;
}

ScopedGameCatalog::~ScopedGameCatalog() {
    g_catalog = m_previous;
}

std::span<const GameCatalogEntry> catalogWithUnsupportedGame() noexcept {
    return kCatalogWithUnsupportedGame;
}

} // namespace testing

} // namespace pmm
