#pragma once

// Import preview and reconciliation for a shared playset, independent of the
// share-code format (sharing/ converts a decoded manifest into entries).
//
// The plan never changes the requested setup: toPlayset() returns exactly the
// requested entries in the requested order with their enabled flags, nothing
// added (extra installed mods stay out of the playset) and nothing dropped
// (failed or missing entries stay in it). Building a plan has no side effects;
// persisting the playset and starting acquisition are separate, later steps.

#include "core/dependencies.hpp"
#include "core/model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace pmm {

enum class ImportEntryStatus {
    Installed,     // installed and usable (Ready, NeedsUpdate or updating)
    Missing,       // Workshop mod not installed: will be acquired
    LocalUnmapped, // local mod not found here: needs manual mapping/installation
    Broken,        // installed but unusable: needs repair, not re-acquired automatically
};
const char* toString(ImportEntryStatus status) noexcept;

struct ImportPlanEntry {
    PlaysetEntry entry; // exactly as requested
    ImportEntryStatus status = ImportEntryStatus::Missing;
    std::string installedName;        // descriptor name when installed
    std::string installedVersion;     // descriptor version when installed
    std::string installedFingerprint; // when installed and computed
    // LocalUnmapped only: installed local mods of the same game with the same
    // fingerprint (listed first) or the same name. Suggestions for the user's
    // explicit mapping; never applied automatically.
    std::vector<ModIdentity> localCandidates;
    std::vector<std::string> warnings;
};

struct ImportCounts {
    std::size_t total = 0;
    std::size_t enabled = 0;
    std::size_t installed = 0;
    std::size_t missing = 0; // status Missing (== missingEnabled + missingDisabled)
    std::size_t missingEnabled = 0;
    std::size_t missingDisabled = 0;
    std::size_t localUnmapped = 0;
    std::size_t broken = 0;
};

struct ImportPlan {
    GameKey game;
    std::string requestedName;
    std::string proposedName; // unique among existing playsets: "Name", "Name (2)", ...
    std::vector<ImportPlanEntry> entries;
    ImportCounts counts;
    // Every missing Workshop entry, disabled ones included, in playset order,
    // without duplicates. Never contains installed content.
    std::vector<ModIdentity> toAcquire;
    // Plan-level notes for the preview. Always ends with the standing note that
    // identical references do not guarantee identical files.
    std::vector<std::string> warnings;

    // The playset to save: proposedName and the requested entries, unchanged.
    Playset toPlayset() const;
};

// Standing note shown with every import.
inline constexpr const char* kIdenticalContentNotGuaranteed =
    "Identical mod references, order and enabled states do not guarantee identical files or "
    "multiplayer checksums: mods and the game can be updated independently.";

ImportPlan buildImportPlan(const GameKey& game, const std::string& playsetName,
                           const std::vector<PlaysetEntry>& requested,
                           const InstalledModMap& inventory,
                           const std::vector<std::string>& existingPlaysetNames,
                           const std::string& installedGameVersion,
                           const std::string& gameVersionHint);

// "Name" if unused, otherwise "Name (2)", "Name (3)", ... Comparison is ASCII
// case-insensitive. An empty name becomes "Imported playset".
std::string uniquePlaysetName(const std::string& name,
                              const std::vector<std::string>& existingNames);

} // namespace pmm
