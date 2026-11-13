#pragma once

// Share-code and Workshop ID list export and import (spec section 5,
// docs/share-code-protocol.md sections 1-8).
//
// Export: every entry of the playset, disabled ones included, in exact order.
// Installed entries carry their installed descriptor name and version as
// hints, installed local mods a content fingerprint (recomputed with the
// injected fingerprint function on every export, because a folder's
// modification time does not reveal edits inside it, and cached in the
// inventory for readiness comparisons until the next rescan), and the code
// carries the installed game version as gameVersionHint. Hints are display
// text only and are sanitised to what the manifest accepts; identities, order
// and enabled flags are copied verbatim. exportWorkshopIdList() lists the
// enabled Workshop entries only, in order, and counts what it left out.
//
// Import is two steps, for a share code or a Workshop ID list alike (text
// starting with "PMM" is a code; a list is imported for the game the caller
// names, with every entry enabled):
//   previewImport()  decodes and validates the complete text and reconciles it
//                    with the cached inventory. Read-only: no database write,
//                    no job, no download. withWorkshopMetadata() adds what the
//                    Steam Web API said about the missing Workshop entries
//                    (workshopIdsToLookUp()); the caller asks, this service
//                    never does.
//   applyImport()    validates again, rebuilds the plan against the current
//                    inventory, and then in ONE database transaction creates
//                    the exact playset (or replaces an explicitly chosen one,
//                    snapshotting its previous state), creates or reuses an
//                    acquisition job for every missing Workshop entry
//                    (disabled ones included, installed usable content never;
//                    none at all without ImportOptions::queueAcquisition)
//                    and makes the playset active. Any failure rolls the whole
//                    transaction back and leaves the database unchanged.
// The imported entries are never reordered, re-flagged, filtered or extended.
// Start the acquisition coordinator for the game afterwards to work the jobs.
//
// Not thread-safe; see service_context.hpp.

#include "core/import_plan.hpp"
#include "core/model.hpp"
#include "core/result.hpp"
#include "providers/web_metadata.hpp"
#include "services/service_context.hpp"
#include "sharing/manifest.hpp"
#include "sharing/workshop_id_list.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::services {

enum class ImportMode {
    New,     // create a new playset (default)
    Replace, // replace the entries of an existing playset (explicit choice; recoverable)
};

// What the Steam Web API said about a missing Workshop entry of a preview
// (providers::classifyWorkshopItemForGame() for the preview's Steam app).
enum class WorkshopItemCheck {
    Ok,          // found, and for the preview's game
    NotFound,    // deleted, private or friends-only (EResult 9)
    Unavailable, // another result without details
    Banned,
    WrongGame, // another game's item
};

struct PreviewWorkshopItem {
    // The Workshop title fitted as a name hint, without direction marks or
    // line separators; empty if none.
    std::string title;
    WorkshopItemCheck check = WorkshopItemCheck::Ok;
    std::string otherGame; // WrongGame: the catalog key of the item's game, if it is one
};

struct ImportPreview {
    std::optional<Error> error; // decoding/validation failure; no manifest or plan then
    sharing::ImportTextFormat format = sharing::ImportTextFormat::ShareCode;
    std::optional<sharing::Manifest> manifest;
    std::optional<ImportPlan> plan;
    std::string gameName;
    bool gameInstalled = false; // an installation of the manifest's game is known
    std::string installedGameVersion;
    std::string steamAppId; // the installation's, else the catalog's
    // withWorkshopMetadata(): the answers for missing Workshop entries, by id.
    std::map<std::string, PreviewWorkshopItem> workshop;
    // How many of plan->warnings withWorkshopMetadata() added; they come right
    // before the standing note, and a later call replaces them.
    std::size_t workshopWarnings = 0;

    bool ok() const noexcept { return !error.has_value(); }
};

// The ids of the preview's missing Workshop entries (disabled ones included),
// in playset order and without repeats: what may be looked up for it. Empty
// for a failed preview.
std::vector<std::string> workshopIdsToLookUp(const ImportPreview& preview);

// `preview` with the Web API's answers for its missing Workshop entries;
// items for other ids are ignored, and an item answered again replaces the
// earlier answer. Pure: nothing is read or written. For each answered entry
// the Workshop title, fitted to the manifest's rules, replaces the name hint
// of the text (it is untrusted) in both the manifest and the plan, so that
// applyImport() stores it. The plan gets warnings for items that were not
// found, have no details, are banned or belong to another game, before the
// standing note; for a Workshop ID list whose found items all belong to one
// other catalog game (and none is installed), one warning names that game.
ImportPreview withWorkshopMetadata(ImportPreview preview,
                                   std::span<const providers::WorkshopItemMetadata> items);

struct ImportOptions {
    // Creates or reuses an acquisition job for every missing Workshop entry.
    // False when Workshop downloads are off: the import then creates and reuses
    // no job (AppliedImport::jobs is empty) and is otherwise identical. Jobs
    // from before it keep their state, and a missing entry reads as its mod's
    // newest job says: "Not installed" without a job, Failed after a failed
    // one, and an unfinished one resumes in a later session with downloads on.
    bool queueAcquisition = true;
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

    // The playset's Workshop ID list (sharing::workshopIdListFromPlayset()).
    Result<sharing::WorkshopIdListExport> exportWorkshopIdList(std::int64_t playsetId);

    // Never writes anything. On failure the preview carries the actionable
    // error. A share code names its game; a Workshop ID list is previewed for
    // `idListGame` (the selected game) and fails without one.
    ImportPreview previewImport(std::string_view text, const GameKey& idListGame = {});

    // `targetPlaysetId` is required for Replace (a live playset of the
    // manifest's game) and ignored for New. `name` overrides the proposed
    // name (New) or renames the replaced playset (Replace, empty keeps it);
    // a new playset's name is made unique, shortened when the " (n)" suffix
    // would not fit into sharing::kMaxPlaysetNameBytes.
    Result<AppliedImport> applyImport(const ImportPreview& preview, ImportMode mode,
                                      std::optional<std::int64_t> targetPlaysetId = std::nullopt,
                                      const std::string& name = {}, ImportOptions options = {});

private:
    // The preview of a decoded or listed manifest against the cached inventory.
    ImportPreview buildPreview(sharing::Manifest manifest, sharing::ImportTextFormat format);

    const ServiceContext& m_ctx;
};

} // namespace pmm::services
