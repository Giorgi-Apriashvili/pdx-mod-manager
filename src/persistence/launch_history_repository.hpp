#pragma once

// Record of game launches, used for diagnostics and to relate a launch to the
// configuration-write transaction that preceded it.

#include "core/model.hpp"
#include "core/result.hpp"
#include "persistence/database.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pmm::persistence {

struct LaunchRecord {
    std::int64_t id = 0;
    GameKey game;
    std::optional<std::int64_t> playsetId;
    std::int64_t startedAt = 0; // unix milliseconds; 0 means "now" when recorded
    std::optional<std::int64_t> pid;
    std::string outcome;       // "started", or errorCodeName() of the failure
    std::string detail;        // short diagnostic text
    std::string transactionId; // configuration-write transaction, if any
};

class LaunchHistoryRepository {
public:
    explicit LaunchHistoryRepository(Database& db) noexcept : m_db(db) {}

    // Returns the new record id.
    Result<std::int64_t> record(const LaunchRecord& record);
    // Up to `limit` records of `game`, newest first.
    Result<std::vector<LaunchRecord>> latest(const GameKey& game, int limit);

private:
    Database& m_db;
};

} // namespace pmm::persistence
