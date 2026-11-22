#pragma once

// Everything the mod list shows for one playset, in load order: each entry
// with its resolved display name and launch readiness, the counts for the
// footer, and the launch verdict for the Play button. Pure derivation from
// the playset, the installed inventory and the acquisition jobs; building a
// view never changes anything.
//
// Readiness follows core/readiness.hpp (installed content wins over the
// state of a job that acquires a missing mod, an unfinished update or
// re-download does not; a job reporting "installed" is not trusted until the
// inventory has seen the content), extended with the adapter's compatibility
// warnings for installed entries and with the session's Workshop findings
// (update available, or refused by the Workshop).

#include "core/dependencies.hpp"
#include "core/model.hpp"
#include "core/readiness.hpp"
#include "core/result.hpp"
#include "games/game_adapter.hpp"
#include "services/service_context.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pmm::services {

struct PlaysetRow {
    std::size_t index = 0; // load-order position, 0-based
    PlaysetEntry entry;
    std::string displayName; // installed descriptor name > nameHint > provider id
    EntryReadiness readiness;
    std::optional<InstalledMod> installed;
    std::optional<AcquisitionJob> job; // most recent job for this identity
    bool blocking = false;             // enabled and not Ready
    // Where each name the installed descriptor lists under dependencies
    // leads (core resolveDependencies()), whatever the game makes of it.
    std::vector<ResolvedDependency> dependencies;
};

struct PlaysetCounts {
    std::size_t total = 0;
    std::size_t enabled = 0;
    std::size_t ready = 0;
    std::size_t missing = 0;  // content not usable locally (StatusFilter::Missing)
    std::size_t warning = 0;  // Broken, or any readiness warning (StatusFilter::Warning)
    std::size_t blocking = 0; // enabled and not Ready
    // Ready entries, enabled or not, with an update available on the Workshop
    // (EntryReadiness::updateAvailable); updatesEnabled counts the enabled ones.
    std::size_t updates = 0;
    std::size_t updatesEnabled = 0;
};

struct PlaysetView {
    std::int64_t playsetId = 0;
    GameKey game;
    std::string name;
    std::vector<PlaysetRow> rows;
    PlaysetCounts counts;
    LaunchReadiness launch; // entry readiness only; LaunchService adds installation checks
    // core checkDependencies(): none in a game whose dependencies are not
    // checked (DependencyMeaning::LoadAfter: EU4, Hearts of Iron IV)
    std::vector<DependencyIssue> dependencyIssues;
};

// `installation` and `adapter` may be null (game not installed / unsupported):
// compatibility warnings are then skipped. `findings` may be null (no update
// check in this session).
PlaysetView buildPlaysetView(const Playset& playset, std::span<const InstalledMod> inventory,
                             std::span<const AcquisitionJob> jobs,
                             const GameInstallation* installation,
                             const games::GameAdapter* adapter,
                             const WorkshopFindings* findings = nullptr);

// Loads the playset, the cached inventory, the stored installation and the
// stored jobs of its game, overlays `liveJobs` (the coordinator's in-memory
// jobs carry fresher progress) and builds the view with `findings`
// (AcquisitionCoordinator::workshopFindings()).
Result<PlaysetView> loadPlaysetView(const ServiceContext& context, std::int64_t playsetId,
                                    std::span<const AcquisitionJob> liveJobs = {},
                                    const WorkshopFindings* findings = nullptr);

} // namespace pmm::services
