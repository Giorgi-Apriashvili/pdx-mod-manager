#pragma once

// Victoria 3 mod metadata: <mod>/.metadata/metadata.json, the descriptor the
// Paradox launcher and the game read instead of Clausewitz .mod files.
// Observed on 1.15.2 (one local mod, nine Workshop items) and in the
// launcher's JSON schema, which requires only "id" and allows other keys:
//   {"name": "...", "id": "", "version": "...",
//    "supported_game_version": "1.15.*", "short_description": "...",
//    "tags": [...], "relationships": [...],
//    "game_custom_data": {"multiplayer_synchronized": true}, ...}
//
// Accepted syntax: strict JSON (no comments, no trailing commas) as the game
// reads it, with an optional UTF-8 BOM, LF or CRLF, tabs or spaces; ill-formed
// UTF-8 becomes U+FFFD (pmm::sanitizeUtf8()) because the inventory is
// persisted as JSON.
//
// Mapping onto ModDescriptor: name, version, picture (as written) and
// supported_game_version (-> supportedVersion) when they are strings; tags
// (the string items); dependencies from "relationships", which comes in two
// forms: objects {rel_type, id, display_name, resource_type, version}, of
// which rel_type "dependency" counts (by display_name, else id; load_before,
// load_after and incompatible_with are not used yet), and the older plain
// list of names. Every other top-level scalar (id, game_id, short_description,
// provider_id, ...) goes to ModDescriptor::extra in file order, numbers and
// booleans as their JSON text. path, archive and remoteFileId stay empty.

#include "core/model.hpp"
#include "core/result.hpp"

#include <filesystem>
#include <string_view>

namespace pmm::games {

namespace fs = std::filesystem;

// The metadata file relative to the mod folder, as the game names it.
inline constexpr std::string_view kModMetadataFile = ".metadata/metadata.json";

struct ModMetadata {
    ModDescriptor descriptor;
    // "game_custom_data" is an object. Without it, the game reportedly logs
    // "Mod metadata read error" and leaves the mod out (unverified).
    bool hasGameCustomData = false;
};

// <folder>/.metadata/metadata.json
fs::path modMetadataPath(const fs::path& modFolder);

// ParseError for invalid JSON (with the line and column) and for a document
// that is not a JSON object.
Result<ModMetadata> parseModMetadata(std::string_view text);

// Reads at most 1 MiB. Errors name the file.
Result<ModMetadata> readModMetadata(const fs::path& file);

} // namespace pmm::games
