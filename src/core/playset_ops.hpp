#pragma once

// Pure editing operations on an ordered playset entry list, plus the view rules
// the mod list uses for searching, filtering and browsing.
//
// Every mutating function validates all of its arguments before changing
// anything: on failure the list is left untouched. Index lists may be given in
// any order and may contain duplicates. Nothing here ever enables, adds or
// reorders an entry the caller did not name.
//
// Load order is only changed by the move functions. Searching, filtering and
// browse sorting change what the view shows, never the list itself; while any
// of them is active reordering is refused (see reorderPolicy()), because a drop
// position in a partial or re-sorted view has no unambiguous load-order meaning.

#include "core/model.hpp"
#include "core/result.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pmm {

// Moves entries[from] so that it ends up at index `to` of the resulting list.
// Both indices must be < entries.size(). InvalidArgument otherwise.
Status moveEntry(std::vector<PlaysetEntry>& entries, std::size_t from, std::size_t to);

// Swap with the neighbour. Moving the first entry up or the last entry down is
// a no-op that succeeds.
Status moveUp(std::vector<PlaysetEntry>& entries, std::size_t index);
Status moveDown(std::vector<PlaysetEntry>& entries, std::size_t index);

// Drag-and-drop of a multi-selection. `insertBefore` is a gap in the ORIGINAL
// list (0 = before the first entry, size() = after the last). The selected
// entries are removed and re-inserted contiguously at that gap, keeping their
// relative order; unselected entries keep theirs. Returns the new index of the
// first moved entry. Dropping onto a gap inside or next to the selection just
// gathers the block there.
Result<std::size_t> moveBlock(std::vector<PlaysetEntry>& entries,
                              std::span<const std::size_t> indices, std::size_t insertBefore);

// Keyboard Move Up / Move Down for a multi-selection: every selected entry
// moves one step unless it is blocked by the list edge or by a selected entry
// that cannot move. Returns the new (sorted) indices of the selection.
Result<std::vector<std::size_t>> moveSelectionUp(std::vector<PlaysetEntry>& entries,
                                                 std::span<const std::size_t> indices);
Result<std::vector<std::size_t>> moveSelectionDown(std::vector<PlaysetEntry>& entries,
                                                   std::span<const std::size_t> indices);

// Sets the enabled flag of the given entries. Returns how many changed.
Result<std::size_t> setEnabled(std::vector<PlaysetEntry>& entries,
                               std::span<const std::size_t> indices, bool enabled);

// A removed entry and the index it had, for undo.
struct RemovedEntry {
    std::size_t index = 0;
    PlaysetEntry entry;

    bool operator==(const RemovedEntry&) const = default;
};

// Removes the given entries. Returns them in ascending original index order;
// passing that result to restoreEntries() undoes the removal exactly.
Result<std::vector<RemovedEntry>> removeEntries(std::vector<PlaysetEntry>& entries,
                                                std::span<const std::size_t> indices);

// Re-inserts entries returned by removeEntries(). Fails with InvalidArgument if
// an index no longer fits, or AlreadyExists if an identity is present again.
Status restoreEntries(std::vector<PlaysetEntry>& entries, std::span<const RemovedEntry> removed);

// Appends `added` in order. Fails with AlreadyExists (adding nothing) if any
// identity is already in the list or repeated within `added`.
Status addEntries(std::vector<PlaysetEntry>& entries, std::span<const PlaysetEntry> added);

std::optional<std::size_t> findEntry(std::span<const PlaysetEntry> entries,
                                     const ModIdentity& identity);

// --- View rules ---------------------------------------------------------------

enum class StatusFilter {
    All,
    Enabled,
    Disabled,
    Missing, // content not usable locally: Missing, Pending, Downloading, Failed,
             // Unavailable or NeedsUserAction
    Warning, // Broken, or any readiness warning
};

struct ViewFilter {
    std::string search; // case-insensitive substring of nameHint or id; trimmed
    StatusFilter status = StatusFilter::All;
    bool browseSorted = false; // view sorted by name instead of load order
};

struct ReorderPolicy {
    bool allowed = true;
    std::string explanation; // why reordering is disabled; empty when allowed
};

// Reordering is allowed only while the view shows the full list in load order.
ReorderPolicy reorderPolicy(const ViewFilter& filter);

// True if the filter shows every entry in load order.
bool isUnfiltered(const ViewFilter& filter);

// Indices of the entries the view shows, in display order: load order, or by
// name (ASCII case-insensitive, then id, then load order) when browseSorted.
// readiness[i] belongs to entries[i]; entries without readiness information
// only match the All, Enabled and Disabled filters. Case folding covers ASCII
// only; other characters must match exactly.
std::vector<std::size_t> visibleIndices(std::span<const PlaysetEntry> entries,
                                        std::span<const EntryReadiness> readiness,
                                        const ViewFilter& filter);

} // namespace pmm
