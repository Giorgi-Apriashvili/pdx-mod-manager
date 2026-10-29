#include "persistence/acquisition_job_repository.hpp"

#include "persistence/internal/mapping.hpp"

namespace pmm::persistence {

using namespace internal;

namespace {

constexpr const char* kSelect =
    "SELECT id, game_key, source, provider_id, playset_id, state, error_code, error_message, "
    "attempts, bytes_downloaded, bytes_total, created_at, updated_at FROM acquisition_jobs";

Result<AcquisitionJob> readJob(const Statement& stmt) {
    AcquisitionJob job;
    job.id = stmt.columnInt64(0);
    job.identity = ModIdentity{stmt.columnText(1), stmt.columnText(2), stmt.columnText(3)};
    job.playsetId = stmt.columnOptionalInt64(4);
    const auto state = acquisitionStateFromString(stmt.columnText(5));
    if (!state)
        return corruptValue("acquisition job " + std::to_string(job.id),
                            "state='" + stmt.columnText(5) + "'");
    job.state = *state;
    job.errorCode = stmt.columnText(6);
    job.errorMessage = stmt.columnText(7);
    job.attempts = stmt.columnInt(8);
    // Byte counters are stored bit-for-bit in signed 64-bit columns.
    job.bytesDownloaded = static_cast<std::uint64_t>(stmt.columnInt64(9));
    job.bytesTotal = static_cast<std::uint64_t>(stmt.columnInt64(10));
    job.createdAt = stmt.columnInt64(11);
    job.updatedAt = stmt.columnInt64(12);
    return job;
}

Result<std::vector<AcquisitionJob>> queryJobs(Statement& stmt) {
    std::vector<AcquisitionJob> out;
    for (;;) {
        PMM_DB_TRY_VALUE(row, stmt.step());
        if (!*row)
            break;
        PMM_DB_TRY_VALUE(job, readJob(stmt));
        out.push_back(std::move(*job));
    }
    return out;
}

Error jobNotFound(std::int64_t id) {
    return makeError(ErrorCode::NotFound, "The download job no longer exists.",
                     "acquisition job id " + std::to_string(id));
}

} // namespace

Result<AcquisitionJob>
AcquisitionJobRepository::createOrReuse(const ModIdentity& identity,
                                        std::optional<std::int64_t> playsetId) {
    if (identity.game.empty() || identity.source.empty() || identity.id.empty())
        return makeError(ErrorCode::InvalidArgument,
                         "Cannot queue a download for an incomplete mod identity.",
                         toString(identity));
    auto guard = m_db.access();
    Connection& c = guard.connection();
    std::optional<AcquisitionJob> result;
    PMM_TRY(m_db.transaction([&]() -> Status {
        PMM_DB_TRY_VALUE(existing,
                         c.prepare(std::string(kSelect) +
                                   " WHERE game_key = ? AND source = ? AND provider_id = ? "
                                   "AND state IN " +
                                   kUnfinishedJobStatesSql + " LIMIT 1"));
        existing->bindAll(identity.game, identity.source, identity.id);
        PMM_DB_TRY_VALUE(found, existing->step());
        if (*found) {
            PMM_DB_TRY_VALUE(job, readJob(*existing));
            result = std::move(*job);
            return {};
        }
        AcquisitionJob job;
        job.identity = identity;
        job.playsetId = playsetId;
        job.state = AcquisitionState::Pending;
        job.createdAt = job.updatedAt = m_db.now();
        PMM_DB_TRY_VALUE(
            insert, c.prepare("INSERT INTO acquisition_jobs (game_key, source, provider_id, "
                              "playset_id, state, attempts, bytes_downloaded, bytes_total, "
                              "created_at, updated_at) VALUES (?, ?, ?, ?, ?, 0, 0, 0, ?, ?)"));
        insert->bindAll(identity.game, identity.source, identity.id, playsetId, toString(job.state),
                        job.createdAt, job.updatedAt);
        PMM_TRY(insert->run());
        job.id = c.lastInsertRowId();
        result = std::move(job);
        return {};
    }));
    return std::move(*result);
}

Status AcquisitionJobRepository::update(const AcquisitionJob& job) {
    auto guard = m_db.access();
    Connection& c = guard.connection();
    PMM_DB_TRY_VALUE(stmt, c.prepare("UPDATE acquisition_jobs SET playset_id = ?, state = ?, "
                                     "error_code = ?, error_message = ?, attempts = ?, "
                                     "bytes_downloaded = ?, bytes_total = ?, updated_at = ? "
                                     "WHERE id = ?"));
    stmt->bindAll(job.playsetId, toString(job.state), job.errorCode, job.errorMessage, job.attempts,
                  job.bytesDownloaded, job.bytesTotal, m_db.now(), job.id);
    if (auto status = stmt->run(); !status) {
        Error error = std::move(status).error();
        if (error.code == ErrorCode::AlreadyExists)
            error.message = "Another download of this mod is already in progress.";
        return error;
    }
    if (c.changes() == 0)
        return jobNotFound(job.id);
    return {};
}

Result<AcquisitionJob> AcquisitionJobRepository::get(std::int64_t id) {
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(stmt, guard.connection().prepare(std::string(kSelect) + " WHERE id = ?"));
    stmt->bind(1, id);
    PMM_DB_TRY_VALUE(row, stmt->step());
    if (!*row)
        return jobNotFound(id);
    return readJob(*stmt);
}

Result<std::vector<AcquisitionJob>> AcquisitionJobRepository::listUnfinished() {
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(stmt, guard.connection().prepare(std::string(kSelect) + " WHERE state IN " +
                                                      kUnfinishedJobStatesSql + " ORDER BY id"));
    return queryJobs(*stmt);
}

Result<std::vector<AcquisitionJob>> AcquisitionJobRepository::listForGame(const GameKey& game) {
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(
        stmt, guard.connection().prepare(std::string(kSelect) + " WHERE game_key = ? ORDER BY id"));
    stmt->bind(1, game);
    return queryJobs(*stmt);
}

Result<int> AcquisitionJobRepository::removeFinishedOlderThan(std::int64_t cutoffUnixMs) {
    auto guard = m_db.access();
    Connection& c = guard.connection();
    PMM_DB_TRY_VALUE(stmt,
                     c.prepare(std::string("DELETE FROM acquisition_jobs WHERE state NOT IN ") +
                               kUnfinishedJobStatesSql + " AND updated_at < ?"));
    PMM_TRY(stmt->bind(1, cutoffUnixMs).run());
    return c.changes();
}

} // namespace pmm::persistence
