#pragma once

// Detected or manually configured game installations, one per game key.

#include "core/model.hpp"
#include "core/result.hpp"
#include "persistence/database.hpp"

#include <optional>
#include <vector>

namespace pmm::persistence {

class InstallationRepository {
public:
    explicit InstallationRepository(Database& db) noexcept : m_db(db) {}

    // Inserts or replaces the installation stored for `installation.game`.
    Status upsert(const GameInstallation& installation);
    // nullopt when no installation is stored for `game`.
    Result<std::optional<GameInstallation>> get(const GameKey& game);
    Result<std::vector<GameInstallation>> list(); // ordered by game key
    Status remove(const GameKey& game);           // succeeds if absent

private:
    Database& m_db;
};

} // namespace pmm::persistence
