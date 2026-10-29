#pragma once

// Share-code manifest, schema version 1: the decoded JSON payload of a share
// code (docs/share-code-protocol.md). Parsing is strict: well-formed UTF-8, a
// root object, no duplicate keys, no unknown keys at any level, exact field
// types and bounded field lengths. Serialization is canonical (fixed key order,
// compact, empty optional fields omitted), so equal manifests always produce
// byte-identical JSON.
//
// The manifest holds references only. Names and version hints are display
// information and are never interpreted as paths, URLs or commands.

#include "core/model.hpp"
#include "core/result.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::sharing {

inline constexpr int kManifestSchemaVersion = 1;

inline constexpr std::size_t kMaxEntries = 10000;
inline constexpr std::size_t kMaxPlaysetNameBytes = 256;
inline constexpr std::size_t kMaxNameHintBytes = 256;
inline constexpr std::size_t kMaxVersionHintBytes = 64;
inline constexpr std::size_t kMaxLocalIdBytes = 128;
inline constexpr std::size_t kMaxWorkshopIdDigits = 20;

struct ManifestEntry {
    std::string source; // provider key, see core/model.hpp source::
    std::string id;     // provider-stable id
    std::string nameHint;
    bool enabled = true;
    std::string versionHint;
    std::string fingerprint; // "sha256:<64 lowercase hex>", informational

    bool operator==(const ManifestEntry&) const = default;
};

struct Manifest {
    int schemaVersion = kManifestSchemaVersion;
    GameKey game;
    std::string playsetName;
    std::string gameVersionHint;
    std::vector<ManifestEntry> mods; // array position is the authoritative order

    bool operator==(const Manifest&) const = default;
};

// Checks every semantic rule of schema v1. Error messages start with the JSON
// path of the offending field, e.g. "mods[3].id: ...". Error codes:
// UnsupportedVersion (schemaVersion != 1), UnsupportedGame (known game without
// an adapter), LimitExceeded (too many entries), ValidationFailed (all else).
Status validateManifest(const Manifest& manifest);

// Parses and validates manifest JSON. Accepts any JSON formatting and key
// order; rejects everything validateManifest() rejects plus ParseError for
// malformed JSON or UTF-8 and ValidationFailed for duplicate keys, unknown keys,
// missing required fields and wrong types. An empty optional string is treated
// like an omitted field.
Result<Manifest> parseManifestJson(std::string_view json);

// Canonical compact JSON (docs/share-code-protocol.md, "Canonical form").
// Validates first and refuses invalid manifests.
Result<std::string> serializeManifestJson(const Manifest& manifest);

} // namespace pmm::sharing
