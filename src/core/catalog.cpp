#include "core/catalog.hpp"

#include "core/model.hpp"

#include <array>

namespace pmm {
namespace {

constexpr std::array<std::string_view, 2> kWorkshopAndLocal{source::SteamWorkshop, source::Local};

constexpr std::array<GameCatalogEntry, 5> kCatalog{{
    {"eu4", "Europa Universalis IV", "236850", "Europa Universalis IV", kWorkshopAndLocal, true},
    {"stellaris", "Stellaris", "281990", "Stellaris", kWorkshopAndLocal, true},
    // Known, not yet supported: listed so the architecture and UI can name them
    // without advertising support.
    {"hoi4", "Hearts of Iron IV", "394360", "Hearts of Iron IV", kWorkshopAndLocal, false},
    {"ck3", "Crusader Kings III", "1158310", "Crusader Kings III", kWorkshopAndLocal, false},
    {"vic3", "Victoria 3", "529340", "Victoria 3", kWorkshopAndLocal, false},
}};

} // namespace

std::span<const GameCatalogEntry> gameCatalog() noexcept {
    return kCatalog;
}

const GameCatalogEntry* findGame(std::string_view key) noexcept {
    for (const auto& entry : kCatalog) {
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

} // namespace pmm
