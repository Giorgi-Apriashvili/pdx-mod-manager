#include "persistence/launch_history_repository.hpp"

#include "persistence/internal/mapping.hpp"

namespace pmm::persistence {

Result<std::int64_t> LaunchHistoryRepository::record(const LaunchRecord& record) {
    if (record.game.empty() || record.outcome.empty())
        return makeError(ErrorCode::InvalidArgument,
                         "A launch record needs a game and an outcome.");
    auto guard = m_db.access();
    Connection& c = guard.connection();
    PMM_DB_TRY_VALUE(stmt, c.prepare("INSERT INTO launch_history (game_key, playset_id, "
                                     "started_at, pid, outcome, detail, transaction_id) "
                                     "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    const std::int64_t startedAt = record.startedAt != 0 ? record.startedAt : m_db.now();
    PMM_TRY(stmt->bindAll(record.game, record.playsetId, startedAt, record.pid, record.outcome,
                          record.detail, record.transactionId)
                .run());
    return c.lastInsertRowId();
}

Result<std::vector<LaunchRecord>> LaunchHistoryRepository::latest(const GameKey& game, int limit) {
    std::vector<LaunchRecord> out;
    if (limit <= 0)
        return out;
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(stmt, guard.connection().prepare(
                               "SELECT id, game_key, playset_id, started_at, pid, outcome, "
                               "detail, transaction_id FROM launch_history WHERE game_key = ? "
                               "ORDER BY started_at DESC, id DESC LIMIT ?"));
    stmt->bindAll(game, limit);
    for (;;) {
        PMM_DB_TRY_VALUE(row, stmt->step());
        if (!*row)
            break;
        LaunchRecord r;
        r.id = stmt->columnInt64(0);
        r.game = stmt->columnText(1);
        r.playsetId = stmt->columnOptionalInt64(2);
        r.startedAt = stmt->columnInt64(3);
        r.pid = stmt->columnOptionalInt64(4);
        r.outcome = stmt->columnText(5);
        r.detail = stmt->columnText(6);
        r.transactionId = stmt->columnText(7);
        out.push_back(std::move(r));
    }
    return out;
}

} // namespace pmm::persistence
