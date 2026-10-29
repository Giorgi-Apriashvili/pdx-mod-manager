#include "core/playset_ops.hpp"

#include <algorithm>
#include <iterator>
#include <set>
#include <string_view>
#include <tuple>
#include <utility>

namespace pmm {
namespace {

Error outOfRange(std::size_t index, std::size_t size) {
    return makeError(ErrorCode::InvalidArgument, "The selected mod is no longer in the playset.",
                     "index " + std::to_string(index) + " out of range (size " +
                         std::to_string(size) + ")");
}

// Sorted, de-duplicated copy of `indices`, all < size.
Result<std::vector<std::size_t>> normalizeIndices(std::span<const std::size_t> indices,
                                                  std::size_t size) {
    std::vector<std::size_t> sorted(indices.begin(), indices.end());
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    if (!sorted.empty() && sorted.back() >= size)
        return outOfRange(sorted.back(), size);
    return sorted;
}

std::vector<bool> selectionMask(const std::vector<std::size_t>& sorted, std::size_t size) {
    std::vector<bool> mask(size, false);
    for (const std::size_t i : sorted)
        mask[i] = true;
    return mask;
}

std::vector<std::size_t> maskIndices(const std::vector<bool>& mask) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < mask.size(); ++i) {
        if (mask[i])
            out.push_back(i);
    }
    return out;
}

char foldAscii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string foldCase(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        c = foldAscii(c);
    return out;
}

std::string_view trimmed(std::string_view text) noexcept {
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    return text;
}

bool isUnresolved(EntryStatus status) noexcept {
    switch (status) {
    case EntryStatus::Missing:
    case EntryStatus::Pending:
    case EntryStatus::Downloading:
    case EntryStatus::Failed:
    case EntryStatus::Unavailable:
    case EntryStatus::NeedsUserAction:
        return true;
    case EntryStatus::Ready:
    case EntryStatus::Broken:
        return false;
    }
    return false;
}

bool matchesStatus(const PlaysetEntry& entry, const EntryReadiness* readiness,
                   StatusFilter filter) {
    switch (filter) {
    case StatusFilter::All:
        return true;
    case StatusFilter::Enabled:
        return entry.enabled;
    case StatusFilter::Disabled:
        return !entry.enabled;
    case StatusFilter::Missing:
        return readiness && isUnresolved(readiness->status);
    case StatusFilter::Warning:
        return readiness &&
               (readiness->status == EntryStatus::Broken || !readiness->warnings.empty());
    }
    return false;
}

} // namespace

Status moveEntry(std::vector<PlaysetEntry>& entries, std::size_t from, std::size_t to) {
    if (from >= entries.size())
        return outOfRange(from, entries.size());
    if (to >= entries.size())
        return outOfRange(to, entries.size());
    const auto first = entries.begin();
    if (from < to)
        std::rotate(first + static_cast<std::ptrdiff_t>(from),
                    first + static_cast<std::ptrdiff_t>(from) + 1,
                    first + static_cast<std::ptrdiff_t>(to) + 1);
    else if (to < from)
        std::rotate(first + static_cast<std::ptrdiff_t>(to),
                    first + static_cast<std::ptrdiff_t>(from),
                    first + static_cast<std::ptrdiff_t>(from) + 1);
    return {};
}

Status moveUp(std::vector<PlaysetEntry>& entries, std::size_t index) {
    if (index >= entries.size())
        return outOfRange(index, entries.size());
    if (index > 0)
        std::swap(entries[index - 1], entries[index]);
    return {};
}

Status moveDown(std::vector<PlaysetEntry>& entries, std::size_t index) {
    if (index >= entries.size())
        return outOfRange(index, entries.size());
    if (index + 1 < entries.size())
        std::swap(entries[index], entries[index + 1]);
    return {};
}

Result<std::size_t> moveBlock(std::vector<PlaysetEntry>& entries,
                              std::span<const std::size_t> indices, std::size_t insertBefore) {
    auto sorted = normalizeIndices(indices, entries.size());
    if (!sorted)
        return std::move(sorted).error();
    if (sorted->empty())
        return makeError(ErrorCode::InvalidArgument, "Select at least one mod to move.");
    if (insertBefore > entries.size())
        return outOfRange(insertBefore, entries.size());

    const std::vector<bool> selected = selectionMask(*sorted, entries.size());
    std::vector<PlaysetEntry> before;
    std::vector<PlaysetEntry> block;
    std::vector<PlaysetEntry> after;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (selected[i])
            block.push_back(std::move(entries[i]));
        else if (i < insertBefore)
            before.push_back(std::move(entries[i]));
        else
            after.push_back(std::move(entries[i]));
    }
    const std::size_t newStart = before.size();
    entries.clear();
    entries.insert(entries.end(), std::make_move_iterator(before.begin()),
                   std::make_move_iterator(before.end()));
    entries.insert(entries.end(), std::make_move_iterator(block.begin()),
                   std::make_move_iterator(block.end()));
    entries.insert(entries.end(), std::make_move_iterator(after.begin()),
                   std::make_move_iterator(after.end()));
    return newStart;
}

Result<std::vector<std::size_t>> moveSelectionUp(std::vector<PlaysetEntry>& entries,
                                                 std::span<const std::size_t> indices) {
    auto sorted = normalizeIndices(indices, entries.size());
    if (!sorted)
        return std::move(sorted).error();
    std::vector<bool> selected = selectionMask(*sorted, entries.size());
    // Ascending pass: a selected entry moves up when the slot above is free,
    // so contiguous blocks move together and blocked entries stay put.
    for (std::size_t i = 1; i < entries.size(); ++i) {
        if (selected[i] && !selected[i - 1]) {
            std::swap(entries[i - 1], entries[i]);
            selected[i - 1] = true;
            selected[i] = false;
        }
    }
    return maskIndices(selected);
}

Result<std::vector<std::size_t>> moveSelectionDown(std::vector<PlaysetEntry>& entries,
                                                   std::span<const std::size_t> indices) {
    auto sorted = normalizeIndices(indices, entries.size());
    if (!sorted)
        return std::move(sorted).error();
    std::vector<bool> selected = selectionMask(*sorted, entries.size());
    for (std::size_t i = entries.size(); i-- > 1;) {
        if (selected[i - 1] && !selected[i]) {
            std::swap(entries[i - 1], entries[i]);
            selected[i] = true;
            selected[i - 1] = false;
        }
    }
    return maskIndices(selected);
}

Result<std::size_t> setEnabled(std::vector<PlaysetEntry>& entries,
                               std::span<const std::size_t> indices, bool enabled) {
    auto sorted = normalizeIndices(indices, entries.size());
    if (!sorted)
        return std::move(sorted).error();
    std::size_t changed = 0;
    for (const std::size_t i : *sorted) {
        if (entries[i].enabled != enabled) {
            entries[i].enabled = enabled;
            ++changed;
        }
    }
    return changed;
}

Result<std::vector<RemovedEntry>> removeEntries(std::vector<PlaysetEntry>& entries,
                                                std::span<const std::size_t> indices) {
    auto sorted = normalizeIndices(indices, entries.size());
    if (!sorted)
        return std::move(sorted).error();
    const std::vector<bool> selected = selectionMask(*sorted, entries.size());
    std::vector<RemovedEntry> removed;
    std::vector<PlaysetEntry> kept;
    kept.reserve(entries.size() - sorted->size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (selected[i])
            removed.push_back(RemovedEntry{i, std::move(entries[i])});
        else
            kept.push_back(std::move(entries[i]));
    }
    entries = std::move(kept);
    return removed;
}

Status restoreEntries(std::vector<PlaysetEntry>& entries, std::span<const RemovedEntry> removed) {
    std::vector<RemovedEntry> sorted(removed.begin(), removed.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const RemovedEntry& a, const RemovedEntry& b) { return a.index < b.index; });

    std::set<ModIdentity> present;
    for (const PlaysetEntry& entry : entries)
        present.insert(entry.identity);
    for (std::size_t k = 0; k < sorted.size(); ++k) {
        // Inserting in ascending order, the k-th entry lands among
        // entries.size() + k existing ones.
        if (sorted[k].index > entries.size() + k || (k > 0 && sorted[k].index == sorted[k - 1].index))
            return makeError(ErrorCode::InvalidArgument,
                             "The removed mods cannot be restored because the playset changed.",
                             "restore index " + std::to_string(sorted[k].index) + " invalid");
        if (!present.insert(sorted[k].entry.identity).second)
            return makeError(ErrorCode::AlreadyExists,
                             "The removed mods cannot be restored: one of them is already in the "
                             "playset again.",
                             toString(sorted[k].entry.identity));
    }
    for (const RemovedEntry& item : sorted)
        entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(item.index), item.entry);
    return {};
}

Status addEntries(std::vector<PlaysetEntry>& entries, std::span<const PlaysetEntry> added) {
    std::set<ModIdentity> present;
    for (const PlaysetEntry& entry : entries)
        present.insert(entry.identity);
    for (const PlaysetEntry& entry : added) {
        if (!present.insert(entry.identity).second) {
            const std::string& name = entry.nameHint.empty() ? entry.identity.id : entry.nameHint;
            return makeError(ErrorCode::AlreadyExists,
                             "\"" + name + "\" is already in this playset; nothing was added.",
                             toString(entry.identity));
        }
    }
    entries.insert(entries.end(), added.begin(), added.end());
    return {};
}

std::optional<std::size_t> findEntry(std::span<const PlaysetEntry> entries,
                                     const ModIdentity& identity) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].identity == identity)
            return i;
    }
    return std::nullopt;
}

bool isUnfiltered(const ViewFilter& filter) {
    return trimmed(filter.search).empty() && filter.status == StatusFilter::All &&
           !filter.browseSorted;
}

ReorderPolicy reorderPolicy(const ViewFilter& filter) {
    const bool filtered = !trimmed(filter.search).empty() || filter.status != StatusFilter::All;
    if (filtered && filter.browseSorted)
        return {false, "Clear the search and filters and switch back to load-order sorting to "
                       "change load order."};
    if (filtered)
        return {false, "Clear the search and filters to change load order."};
    if (filter.browseSorted)
        return {false, "Switch back to load-order sorting to change load order. Sorting by name "
                       "only changes the view."};
    return {true, {}};
}

std::vector<std::size_t> visibleIndices(std::span<const PlaysetEntry> entries,
                                        std::span<const EntryReadiness> readiness,
                                        const ViewFilter& filter) {
    const std::string needle = foldCase(trimmed(filter.search));
    std::vector<std::size_t> visible;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const PlaysetEntry& entry = entries[i];
        const EntryReadiness* r = i < readiness.size() ? &readiness[i] : nullptr;
        if (!matchesStatus(entry, r, filter.status))
            continue;
        if (!needle.empty() && foldCase(entry.nameHint).find(needle) == std::string::npos &&
            foldCase(entry.identity.id).find(needle) == std::string::npos)
            continue;
        visible.push_back(i);
    }

    if (filter.browseSorted) {
        std::vector<std::tuple<std::string, std::string, std::size_t>> keyed;
        keyed.reserve(visible.size());
        for (const std::size_t i : visible) {
            const PlaysetEntry& entry = entries[i];
            keyed.emplace_back(foldCase(entry.nameHint.empty() ? entry.identity.id : entry.nameHint),
                               entry.identity.id, i);
        }
        std::sort(keyed.begin(), keyed.end());
        for (std::size_t k = 0; k < keyed.size(); ++k)
            visible[k] = std::get<2>(keyed[k]);
    }
    return visible;
}

} // namespace pmm
