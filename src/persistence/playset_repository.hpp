#pragma once

// Named, ordered playsets per game, with an undo history for destructive
// changes.
//
// Invariants:
//   - Entries are stored with positions 0..N-1 in exactly the given order;
//     identities are unique within a playset and belong to the playset's game
//     (an empty ModIdentity::game is accepted and read back as the playset's).
//   - Every multi-row change is one transaction: on failure the previous
//     state is intact.
//   - Deletion is soft: remove() snapshots the playset into playset_history
//     and hides it. restore() brings back the exact name and entries.
//   - Ordinary edits (saveEntries, rename) do not create history; callers use
//     replaceWithSnapshot() for imports that replace a playset and for bulk
//     destructive edits so the previous state stays recoverable.

#include "core/model.hpp"
#include "core/result.hpp"
#include "persistence/database.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::persistence {

// History reasons. Callers may pass other short snake_case reasons to
// replaceWithSnapshot().
namespace history_reason {
inline constexpr const char* Deleted = "deleted";              // remove()
inline constexpr const char* BeforeRestore = "before_restore"; // restore() over a live playset
inline constexpr const char* ImportReplace = "import_replace"; // share-code import replaced it
inline constexpr const char* BulkEdit = "bulk_edit";           // bulk enable/disable/reorder
} // namespace history_reason

struct PlaysetHistoryEntry {
    std::int64_t id = 0;
    std::int64_t playsetId = 0; // playset the snapshot was taken from
    GameKey game;
    std::string reason;
    std::int64_t createdAt = 0; // unix milliseconds
    Playset snapshot;           // name, entries and timestamps as they were; id == playsetId
};

class PlaysetRepository {
public:
    explicit PlaysetRepository(Database& db) noexcept : m_db(db) {}

    // Persists a new playset with its entries; `playset.id` and timestamps
    // are ignored (both timestamps are set to now). Returns the new id.
    Result<std::int64_t> create(const Playset& playset);
    // NotFound for unknown or deleted playsets.
    Result<Playset> get(std::int64_t id);
    // Live playsets of `game`, ordered by name (case-insensitive), then id.
    Result<std::vector<PlaysetSummary>> listForGame(const GameKey& game);

    // Replaces all entries atomically and bumps updated_at.
    Status saveEntries(std::int64_t id, const std::vector<PlaysetEntry>& entries);
    Status rename(std::int64_t id, std::string_view name);
    // Copies entries into a new playset; returns its id.
    Result<std::int64_t> duplicate(std::int64_t id, std::string_view newName);
    // Snapshots the playset (reason "deleted") and soft-deletes it. Returns the
    // history entry id, which restore() accepts to undo the deletion.
    Result<std::int64_t> remove(std::int64_t id);
    // Snapshots the current state with `reason`, then replaces all entries.
    // Returns the history entry id.
    Result<std::int64_t> replaceWithSnapshot(std::int64_t id,
                                             const std::vector<PlaysetEntry>& newEntries,
                                             std::string_view reason);

    // Newest first (by creation time, then id).
    Result<std::vector<PlaysetHistoryEntry>> historyForGame(const GameKey& game);
    Result<std::vector<PlaysetHistoryEntry>> historyForPlayset(std::int64_t playsetId);
    Result<PlaysetHistoryEntry> historyEntry(std::int64_t historyId);

    // Restores the snapshot's exact name and entries. If the playset still
    // exists it is undeleted (a live playset is first snapshotted with reason
    // "before_restore", so a restore is itself undoable); if it was purged a
    // new playset is created. Returns the restored playset's id.
    Result<std::int64_t> restore(std::int64_t historyId);

    // Deletes history entries created before `cutoffUnixMs`, always keeping
    // the `keepAtLeast` newest entries of each playset. Soft-deleted playsets
    // left without any history are then removed for good. Returns the number
    // of history entries deleted.
    Result<int> purgeHistoryOlderThan(std::int64_t cutoffUnixMs, int keepAtLeast);

private:
    Database& m_db;
};

} // namespace pmm::persistence
