#pragma once

// Dependency checks for a playset. Paradox descriptors list dependencies by
// display NAME, so a dependency is resolved by matching names: first against the
// playset (installed descriptor name, falling back to the entry's nameHint),
// then against the installed inventory of the same game. Exact matches win;
// an ASCII case-insensitive match is accepted when no exact match exists.
//
// What a dependency means depends on the game (GameCatalogEntry's
// dependencyMeaning). Where it is Required, only ENABLED entries that are
// installed (and therefore have a descriptor) are checked. Dependencies load
// first: index 0 is handed to the game first (see core/model.hpp), so a
// dependency must have a lower index than its dependent. Where it is LoadAfter
// (Hearts of Iron IV), the game loads a mod after the named mods when they
// are enabled and does not need them, so nothing is reported;
// resolveDependencies() still says where each name leads.
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
    std::size_t entryIndex = 0; // the dependent entry
    std::string dependencyName; // as written in the descriptor (trimmed)
    DependencyIssueKind kind = DependencyIssueKind::NotInstalled;
    std::optional<std::size_t> dependencyIndex; // playset position, when in the playset
    std::string message;                        // user-facing, names the dependency

    bool operator==(const DependencyIssue&) const = default;
};

// Where a dependency name leads, whatever the game makes of it.
enum class DependencyResolution {
    EnabledEarlier, // an enabled playset entry before the dependent
    EnabledLater,   // enabled playset entries, all of them after the dependent
    Disabled,       // playset entries, none of them enabled
    NotInPlayset,   // an installed mod of the game, not part of this playset
    NotInstalled,   // no installed mod of the game has that name
};

struct ResolvedDependency {
    std::string name; // as written in the descriptor (trimmed)
    DependencyResolution resolution = DependencyResolution::NotInstalled;
    // The playset position that decided it: the first enabled entry before
    // the dependent (EnabledEarlier), the first enabled one after it
    // (EnabledLater), the first entry of that name (Disabled); none otherwise.
    std::optional<std::size_t> index;

    bool operator==(const ResolvedDependency&) const = default;
};

// For every entry (result[i] belongs to entries[i], enabled or not): the
// non-empty names its installed descriptor lists, in the descriptor's order,
// with where each leads, each mod once: a later name is left out when it
// leads to the same playset entry as an earlier one or, leading to none,
// matches an earlier name up to ASCII case (checkDependencies() still reports
// every exact spelling). A name that matches only the entry itself is left
// out. Empty for an entry that is not installed.
std::vector<std::vector<ResolvedDependency>>
resolveDependencies(std::span<const PlaysetEntry> entries, const InstalledModMap& inventory);

// Issues for every enabled, installed entry of a game whose dependencies are
// Required (none for a LoadAfter game), ordered by entryIndex and then by the
// order of the descriptor's dependency list.
std::vector<DependencyIssue> checkDependencies(std::span<const PlaysetEntry> entries,
                                               const InstalledModMap& inventory);

// Feedback for a bulk enable: the issues that would exist after enabling
// `indices` but do not exist now (same entry, dependency name and kind).
// Out-of-range indices are ignored. Does not modify `entries`.
std::vector<DependencyIssue>
dependencyIssuesIntroducedByEnabling(std::span<const PlaysetEntry> entries,
                                     const InstalledModMap& inventory,
                                     std::span<const std::size_t> indices);

} // namespace pmm
