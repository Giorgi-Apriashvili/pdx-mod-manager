#pragma once

// Persistent acquisition jobs (subscribe/download of missing mods), so pending
// work survives restarts and can be reconciled with the provider.
//
// Invariant: at most one unfinished (non-terminal, see pmm::isTerminal) job
// per mod identity. It is enforced by a partial unique index, so concurrent
// writers cannot create duplicates; createOrReuse() returns the existing job.

#include "core/model.hpp"
#include "core/result.hpp"
#include "persistence/database.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace pmm::persistence {

class AcquisitionJobRepository {
public:
    explicit AcquisitionJobRepository(Database& db) noexcept : m_db(db) {}

    // Returns the unfinished job for `identity` if there is one (unchanged),
    // otherwise creates a Pending job linked to `playsetId`.
    Result<AcquisitionJob> createOrReuse(const ModIdentity& identity,
                                         std::optional<std::int64_t> playsetId);
    // Persists the mutable fields (playsetId, state, error, attempts, bytes)
    // of the job with `job.id` and sets updatedAt to now. The identity cannot
    // change. NotFound for unknown ids; AlreadyExists when moving a finished
    // job back to an unfinished state while another unfinished job exists.
    Status update(const AcquisitionJob& job);
    Result<AcquisitionJob> get(std::int64_t id); // NotFound for unknown ids
    // Non-terminal jobs in creation order, for reconciliation after a restart.
    Result<std::vector<AcquisitionJob>> listUnfinished();
    Result<std::vector<AcquisitionJob>> listForGame(const GameKey& game); // creation order
    // Deletes terminal jobs last updated before `cutoffUnixMs`; returns count.
    Result<int> removeFinishedOlderThan(std::int64_t cutoffUnixMs);

private:
    Database& m_db;
};

} // namespace pmm::persistence
