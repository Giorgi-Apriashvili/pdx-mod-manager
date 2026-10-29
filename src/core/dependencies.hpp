#pragma once

// Dependency checks for a playset. Paradox descriptors list dependencies by
// display NAME, so a dependency is resolved by matching names: first against the
// playset (installed descriptor name, falling back to the entry's nameHint),
// then against the installed inventory of the same game. Exact matches win;
// an ASCII case-insensitive match is accepted when no exact match exists.
//
// Only ENABLED entries that are installed (and therefore have a descriptor) are
// checked. Dependencies load first: index 0 is handed to the game first (see
// core/model.hpp), so a dependency must have a lower index than its dependent.
//
// These functions only report. They never enable, add or reorder anything;
// fixing an issue is an explicit user action.

#include "core/model.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pmm {

// Installed inventory keyed by identity. Shared by readiness and import planning.
using InstalledModMap = std::map<ModIdentity, InstalledMod>;

enum class DependencyIssueKind {
    NotInstalled, // no installed mod of the game has that name
    NotInPlayset, // installed, but not part of this playset
    Disabled,     // in the playset, but disabled
    OrderedAfter, // enabled, but loads after the dependent
};
const char* toString(DependencyIssueKind kind) noexcept;

struct DependencyIssue {
    std::size_t entryIndex = 0;                 // the dependent entry
    std::string dependencyName;                 // as written in the descriptor (trimmed)
    DependencyIssueKind kind = DependencyIssueKind::NotInstalled;
    std::optional<std::size_t> dependencyIndex; // playset position, when in the playset
    std::string message;                        // user-facing, names the dependency

    bool operator==(const DependencyIssue&) const = default;
};

// Issues for every enabled, installed entry, ordered by entryIndex and then by
// the order of the descriptor's dependency list.
std::vector<DependencyIssue> checkDependencies(std::span<const PlaysetEntry> entries,
                                               const InstalledModMap& inventory);

// Feedback for a bulk enable: the issues that would exist after enabling
// `indices` but do not exist now (same entry, dependency name and kind).
// Out-of-range indices are ignored. Does not modify `entries`.
std::vector<DependencyIssue> dependencyIssuesIntroducedByEnabling(
    std::span<const PlaysetEntry> entries, const InstalledModMap& inventory,
    std::span<const std::size_t> indices);

} // namespace pmm
