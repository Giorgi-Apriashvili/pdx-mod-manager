#pragma once

// Share-code export and import (spec section 5).
//
// Export: every entry of the playset, disabled ones included, in exact order.
// Installed entries carry their installed descriptor name and version as
// hints, installed local mods a content fingerprint (recomputed with the
// injected fingerprint function on every export, because a folder's
// modification time does not reveal edits inside it, and cached in the
// inventory for readiness comparisons until the next rescan), and the code
// carries the installed game version as gameVersionHint. Hints are display
// text only and are sanitised to what the manifest accepts; identities, order
// and enabled flags are copied verbatim.
//
// Import is two steps:
//   previewImport()  decodes and validates the complete code and reconciles it
//                    with the cached inventory. Read-only: no database write,
//                    no job, no download.
//   applyImport()    validates again, rebuilds the plan against the current
//                    inventory, and then in ONE database transaction creates
//                    the exact playset (or replaces an explicitly chosen one,
//                    snapshotting its previous state), creates or reuses an
//                    acquisition job for every missing Workshop entry
//                    (disabled ones included, installed usable content never)
//                    and makes the playset active. Any failure rolls the whole
//                    transaction back and leaves the database unchanged.
// The imported entries are never reordered, re-flagged, filtered or extended.
// Start the acquisition coordinator for the game afterwards to work the jobs.
//
// Not thread-safe; see service_context.hpp.

#include "core/import_plan.hpp"
#include "core/model.hpp"
#include "core/result.hpp"
#include "services/service_context.hpp"
#include "sharing/manifest.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::services {

enum class ImportMode {
    New,     // create a new playset (default)
    Replace, // replace the entries of an existing playset (explicit choice; recoverable)
};

struct ImportPreview {
    std::optional<Error> error; // decoding/validation failure; nothing else is set then
    std::optional<sharing::Manifest> manifest;
    std::optional<ImportPlan> plan;
    std::string gameName;
    bool gameInstalled = false; // an installation of the manifest's game is known
    std::string installedGameVersion;

    bool ok() const noexcept { return !error.has_value(); }
};

struct AppliedImport {
    std::int64_t playsetId = 0;
    std::string playsetName;
    ImportMode mode = ImportMode::New;
    std::optional<std::int64_t> replacedHistoryId; // Replace: snapshot of the previous state
    std::vector<AcquisitionJob> jobs;              // created or reused, in playset order
    ImportPlan plan;                               // as applied (current inventory)
};

class ShareService {
public:
    explicit ShareService(const ServiceContext& context) noexcept : m_ctx(context) {}

    // Code for the playset. Fails with the manifest validation error when an
    // identity cannot be shared (for example a malformed local id).
    Result<std::string> exportCode(std::int64_t playsetId);

    // Never writes anything. On failure the preview carries the actionable error.
    ImportPreview previewImport(std::string_view text);

    // `targetPlaysetId` is required for Replace (a live playset of the
    // manifest's game) and ignored for New. `name` overrides the proposed
    // name (New) or renames the replaced playset (Replace, empty keeps it);
    // a new playset's name is made unique, shortened when the " (n)" suffix
    // would not fit into sharing::kMaxPlaysetNameBytes.
    Result<AppliedImport> applyImport(const ImportPreview& preview, ImportMode mode,
                                      std::optional<std::int64_t> targetPlaysetId = std::nullopt,
                                      const std::string& name = {});

private:
    const ServiceContext& m_ctx;
};

} // namespace pmm::services
