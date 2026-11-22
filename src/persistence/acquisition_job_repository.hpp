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
#include <string>
#include <vector>

namespace pmm::persistence {

class AcquisitionJobRepository {
public:
    // What a new job does (AcquisitionJob::kind and the update target).
    struct NewJob {
        AcquisitionKind kind = AcquisitionKind::Acquire;
        std::int64_t targetTimeUpdated = 0; // unix seconds; update jobs
        std::string targetManifest;         // update jobs
    };

    explicit AcquisitionJobRepository(Database& db) noexcept : m_db(db) {}

    // Returns the unfinished job for `identity` if there is one (unchanged,
    // whatever its kind), otherwise creates a Pending job of `kind` linked to
    // `playsetId`. The first form creates an acquire job.
    Result<AcquisitionJob> createOrReuse(const ModIdentity& identity,
                                         std::optional<std::int64_t> playsetId);
    Result<AcquisitionJob> createOrReuse(const ModIdentity& identity,
                                         std::optional<std::int64_t> playsetId, const NewJob& kind);
    // Persists the mutable fields (playsetId, state, error, attempts, bytes,
    // update target, reinstall step) of the job with `job.id` and sets
    // updatedAt to now. The identity and the kind cannot change. NotFound for
    // unknown ids; AlreadyExists when moving a finished job back to an
    // unfinished state while another unfinished job exists.
    Status update(const AcquisitionJob& job);
    Result<AcquisitionJob> get(std::int64_t id); // NotFound for unknown ids
    // Non-terminal jobs in creation order, for reconciliation after a restart.
    Result<std::vector<AcquisitionJob>> listUnfinished();
    Result<std::vector<AcquisitionJob>> listForGame(const GameKey& game); // creation order
    // Deletes terminal jobs last updated before `cutoffUnixMs`; returns count.
    Result<int> removeFinishedOlderThan(std::int64_t cutoffUnixMs);
    // Deletes the jobs that an import with downloads off created and withdrew
    // in the same transaction (versions 0.1.0 and 0.1.1): Cancelled with no
    // error, no attempt and no byte. A cancellation by the user always records
    // the error code "cancelled" and a message, so those jobs are kept; nothing
    // else writes the Cancelled state. Returns the count.
    Result<int> removeWithdrawnImportJobs();

private:
    Database& m_db;
};

} // namespace pmm::persistence
