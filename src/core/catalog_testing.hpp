#pragma once

// Test-only hooks for the game catalog. Production code must not use these:
// they replace the catalog that gameCatalog(), findGame() and
// isSourceValidForGame() read, so that the paths for a known game without an
// adapter (UnsupportedGame, "Other games") stay tested while every real
// catalog entry is supported.

#include "core/catalog.hpp"

#include <span>

namespace pmm::testing {

// The key of the synthetic unsupported game in catalogWithUnsupportedGame().
inline constexpr std::string_view kUnsupportedGameKey = "pmm-test-unsupported";

// Makes `entries` the catalog until the guard is destroyed, then restores the
// previous one. Create it before any thread that reads the catalog starts
// and destroy it after they have stopped: the swap itself is not
// synchronised. Entries returned by findGame() meanwhile point into
// `entries`, so do not compare them with addresses taken outside the guard
// (e.g. GameAdapter::catalog() of an adapter constructed before it).
class ScopedGameCatalog {
public:
    explicit ScopedGameCatalog(std::span<const GameCatalogEntry> entries) noexcept;
    ~ScopedGameCatalog();
    ScopedGameCatalog(const ScopedGameCatalog&) = delete;
    ScopedGameCatalog& operator=(const ScopedGameCatalog&) = delete;

private:
    std::span<const GameCatalogEntry> m_previous;
};

// The real catalog followed by {"pmm-test-unsupported", "Example Unsupported
// Game", Steam app "1", user-data folder "Example Unsupported Game", Steam
// Workshop and local mods, no adapter, dependencies Required}. Static storage.
std::span<const GameCatalogEntry> catalogWithUnsupportedGame() noexcept;

} // namespace pmm::testing
