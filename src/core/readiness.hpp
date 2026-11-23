#pragma once

// Launch readiness of playset entries, derived from the installed inventory and
// acquisition jobs. Installed, usable content wins over the state of a job that
// acquires a missing mod; a job reporting "installed" is not trusted until the
// inventory has seen the content. An unfinished update or re-download of an
// installed mod (AcquisitionKind Update / Reinstall) is the exception: Steam is
// about to change its files, so the entry is not Ready until the job finished.
//
// Mapping (entry -> EntryStatus):
//   installed, unfinished update/reinstall job -> Pending, or Downloading (progress)
//   installed, Ready or NeedsUpdate      -> Ready (+ warnings: a local mod's
//                                           files differ from the entry's
//                                           fingerprint, scan problems, a failed
//                                           update or re-download unless
//                                           updateJobResolved()); NeedsUpdate
//                                           sets updateAvailable
//   installed, Downloading               -> Downloading (provider is updating it)
//   installed, Broken                    -> Broken
//   not installed, job Pending/Unsubscribing/Subscribing/Subscribed/DownloadRequested -> Pending
//   not installed, job Downloading       -> Downloading (progress when known)
//   not installed, job Installed         -> Pending ("verifying")
//   not installed, job Failed            -> Failed
//   not installed, job Unavailable       -> Unavailable
//   not installed, job NeedsUserAction   -> NeedsUserAction
//   not installed, job Cancelled         -> Missing
//   not installed, no job, local source  -> NeedsUserAction (map/install manually)
//   not installed, no job, workshop      -> Missing
//
// Launching requires every ENABLED entry to be Ready. Disabled entries never
// block, whatever their state; they stay visible as unresolved. An update
// available on the Workshop never blocks.
//
// An installed version other than the entry's versionHint is no warning
// (the owner's decision of 2026-11-24): only the import preview shows a
// share code's versions (core/import_plan.hpp).

#include "core/dependencies.hpp"
#include "core/model.hpp"

#include <cstddef>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace pmm {

// What this session's check of installed Workshop mods against the Steam
// Web API found (services::AcquisitionCoordinator::workshopFindings()).
struct WorkshopFindings {
    // A newer version is on the Workshop than the installed one.
    std::set<ModIdentity> updates;
    // The Workshop refuses the item (removed, private, banned, another game):
    // why, for the entry's warnings. Such an item gets no update.
    std::map<ModIdentity, std::string> refused;
};

// Whether an update job that did not finish (Failed, NeedsUserAction) has
// nothing left to do: the installed mod reached the job's target (Steam's
// record of the installed version is at least its time_updated), or, for a
// job without a target, Steam no longer flags the mod for an update. Steam
// updates subscribed items by itself, so this is the usual end of an
// update it had not picked up yet.
bool updateJobResolved(const AcquisitionJob& job, const InstalledMod& installed) noexcept;

// `installed` and `job` may be null. A job for another identity is ignored.
EntryReadiness computeReadiness(const PlaysetEntry& entry, const InstalledMod* installed,
                                const AcquisitionJob* job);

// Readiness for a whole playset: per-entry readiness (using the most recent job
// per identity, by updatedAt then id) plus the dependency warnings of
// checkDependencies() for enabled entries, as the game's dependencyMeaning
// says: every kind where they are required (Victoria 3), only a named mod
// ordered after its dependent in Stellaris and Crusader Kings III, none in
// Europa Universalis IV and Hearts of Iron IV (see core/dependencies.hpp).
// With `findings`, Ready entries it lists as updated get updateAvailable, and
// the ones it lists as refused a warning instead (and no update).
// Result[i] belongs to entries[i].
std::vector<EntryReadiness> computePlaysetReadiness(std::span<const PlaysetEntry> entries,
                                                    const InstalledModMap& inventory,
                                                    std::span<const AcquisitionJob> jobs,
                                                    const WorkshopFindings* findings = nullptr);

struct LaunchBlocker {
    std::size_t index = 0; // playset position, 0-based
    std::string name;      // nameHint, or the id when there is no hint
    EntryStatus status = EntryStatus::Missing;
    std::string reason; // "#3 Example mod: missing - not installed..."
};

struct LaunchReadiness {
    bool canLaunch = false;
    std::vector<LaunchBlocker> blockers; // enabled entries that are not Ready
    std::size_t enabledCount = 0;
    std::size_t unresolvedDisabledCount = 0; // disabled entries that are not Ready
    std::string summary;                     // one line for the Play button area
};

// readiness[i] belongs to entries[i]; an enabled entry without readiness
// information blocks launching.
LaunchReadiness evaluateLaunch(std::span<const PlaysetEntry> entries,
                               std::span<const EntryReadiness> readiness);

} // namespace pmm
