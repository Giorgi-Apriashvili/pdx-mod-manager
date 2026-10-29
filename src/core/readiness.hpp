#pragma once

// Launch readiness of playset entries, derived from the installed inventory and
// acquisition jobs. Installed, usable content always wins over job state; a job
// reporting "installed" is not trusted until the inventory has seen the content.
//
// Mapping (entry -> EntryStatus):
//   installed, Ready or NeedsUpdate      -> Ready (+ warnings: version/fingerprint
//                                           mismatch, pending update, scan problems)
//   installed, Downloading               -> Downloading (provider is updating it)
//   installed, Broken                    -> Broken
//   not installed, job Pending/Subscribing/Subscribed/DownloadRequested -> Pending
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
// block, whatever their state; they stay visible as unresolved.

#include "core/dependencies.hpp"
#include "core/model.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace pmm {

// `installed` and `job` may be null. A job for another identity is ignored.
EntryReadiness computeReadiness(const PlaysetEntry& entry, const InstalledMod* installed,
                                const AcquisitionJob* job);

// Readiness for a whole playset: per-entry readiness (using the most recent job
// per identity, by updatedAt then id) plus dependency warnings for enabled
// entries (see core/dependencies.hpp). Result[i] belongs to entries[i].
std::vector<EntryReadiness> computePlaysetReadiness(std::span<const PlaysetEntry> entries,
                                                    const InstalledModMap& inventory,
                                                    std::span<const AcquisitionJob> jobs);

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
