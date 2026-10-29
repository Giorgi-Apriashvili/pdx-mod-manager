#pragma once

// Playset management and editing (spec sections 3 and 4).
//
// Management: list, create, duplicate, rename, remove (soft, snapshotted),
// restore, and the active playset per game, persisted in the settings table
// ("game.<key>.activePlaysetId", plus "app.lastGameKey").
//
// Editing works on one playset (the one the mod list shows, i.e. the active
// playset) through core/playset_ops. Every edit is persisted immediately and
// pushed onto an in-memory undo stack for that playset (at most kUndoLimit
// steps, oldest dropped first) with redo; undo and redo persist too. If the
// stored entries no longer match what the stack expects (an import replaced
// the playset, a restore happened, another instance edited it), undo/redo
// refuse and clear that playset's stacks instead of overwriting the change.
//
// Removing entries is destructive, so the previous state is additionally
// snapshotted into the playset history ("entries_removed") before saving.
// Reordering takes the view's filter and is refused with the reorderPolicy()
// explanation while the view is searched, filtered or browse-sorted.
//
// Not thread-safe; see service_context.hpp.

#include "core/dependencies.hpp"
#include "core/model.hpp"
#include "core/playset_ops.hpp"
#include "core/result.hpp"
#include "persistence/playset_repository.hpp"
#include "services/service_context.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pmm::services {

// History reason used by removeEntries().
inline constexpr const char* kHistoryReasonEntriesRemoved = "entries_removed";

struct UndoInfo {
    bool canUndo = false;
    bool canRedo = false;
    std::string undoText; // label of the step undo() reverts, e.g. "Disable 3 mods"
    std::string redoText;
};

class PlaysetService {
public:
    static constexpr std::size_t kUndoLimit = 100;
    static constexpr const char* kDefaultName = "New playset";

    explicit PlaysetService(const ServiceContext& context) noexcept : m_ctx(context) {}

    Result<std::vector<PlaysetSummary>> listPlaysets(const GameKey& game);
    Result<Playset> playset(std::int64_t playsetId);

    // The stored active playset when it still exists, otherwise the first
    // playset of the game (not persisted), otherwise nullopt.
    Result<std::optional<std::int64_t>> activePlaysetId(const GameKey& game);
    // Also records the game as "app.lastGameKey". NotFound/InvalidArgument
    // when the playset does not exist or belongs to another game.
    Status setActivePlayset(const GameKey& game, std::int64_t playsetId);
    Result<std::optional<GameKey>> lastGameKey();
    Status setLastGameKey(const GameKey& game);

    // Names are trimmed; at most 256 bytes. create() and duplicate() make the
    // name unique ("Name (2)", shortening the name when the suffix would not
    // fit); an empty name gets a default. They do not change the active
    // playset.
    Result<std::int64_t> create(const GameKey& game, const std::string& name);
    Result<std::int64_t> duplicate(std::int64_t playsetId, const std::string& name);
    // AlreadyExists when another playset of the game has that name.
    Status rename(std::int64_t playsetId, const std::string& name);
    // Soft delete with a snapshot; returns the history id. If it was the
    // active playset, another playset of the game becomes active (if any).
    Result<std::int64_t> remove(std::int64_t playsetId);
    // Newest "deleted" snapshot whose playset is still deleted.
    Result<std::optional<persistence::PlaysetHistoryEntry>> lastDeleted(const GameKey& game);
    // Restores lastDeleted() and makes it active. NotFound when there is none.
    Result<std::int64_t> restoreLastDeleted(const GameKey& game);
    Result<std::vector<persistence::PlaysetHistoryEntry>> history(const GameKey& game);
    // Restores any snapshot (exact name and entries) and makes it active.
    Result<std::int64_t> restoreHistory(std::int64_t historyId);

    // --- Editing (indices are load-order positions, 0-based) ------------------

    // Returns the dependency issues the change introduces (empty when none).
    Result<std::vector<DependencyIssue>>
    setEnabled(std::int64_t playsetId, std::span<const std::size_t> indices, bool enabled);
    Status move(std::int64_t playsetId, std::size_t from, std::size_t to, const ViewFilter& filter);
    // Keyboard Move Up/Down for a selection; returns the selection's new indices.
    Result<std::vector<std::size_t>>
    moveUp(std::int64_t playsetId, std::span<const std::size_t> indices, const ViewFilter& filter);
    Result<std::vector<std::size_t>> moveDown(std::int64_t playsetId,
                                              std::span<const std::size_t> indices,
                                              const ViewFilter& filter);
    // Drag and drop of a selection (see core moveBlock()); returns the new
    // index of the first moved entry.
    Result<std::size_t> moveBlock(std::int64_t playsetId, std::span<const std::size_t> indices,
                                  std::size_t insertBefore, const ViewFilter& filter);
    // Appends installed mods of the playset's game, enabled, in the given
    // order. NotFound when one is not installed; AlreadyExists (nothing added)
    // when one is already in the playset.
    Status addMods(std::int64_t playsetId, std::span<const ModIdentity> identities);
    // Snapshots the playset into its history, then removes the entries.
    Status removeEntries(std::int64_t playsetId, std::span<const std::size_t> indices);

    Status undo(std::int64_t playsetId);
    Status redo(std::int64_t playsetId);
    UndoInfo undoInfo(std::int64_t playsetId) const;
    void clearUndoHistory(std::int64_t playsetId);

private:
    struct Step {
        std::string label;
        std::vector<PlaysetEntry> entries; // state this step goes back (or forward) to
        std::size_t expectedHash = 0;      // hash of the stored state it applies to
    };
    struct Stacks {
        std::deque<Step> undo;
        std::vector<Step> redo;
    };

    // Persists `after` (or snapshots and replaces when `historyReason` is set)
    // and records the step. No-op when nothing changed.
    Status commit(std::int64_t playsetId, std::string label,
                  const std::vector<PlaysetEntry>& before, const std::vector<PlaysetEntry>& after,
                  const char* historyReason = nullptr);
    Result<Playset> load(std::int64_t playsetId);
    Result<std::string> checkedName(const std::string& name) const;

    const ServiceContext& m_ctx;
    std::map<std::int64_t, Stacks> m_stacks;
};

} // namespace pmm::services
