#pragma once

// Share-code protocol version 1: `PMM1.<payload>.<digest>` where payload is the
// unpadded Base64URL encoding of the zlib-compressed canonical manifest JSON and
// digest is the lowercase hex SHA-256 of the compressed bytes. The digest only
// detects accidental corruption; it does not prove who created a code.
//
// Decoding checks, in order: input size, whitespace trim, envelope version,
// part structure, digest format, Base64URL, digest match, bounded inflation,
// UTF-8, JSON structure and finally the manifest schema. Nothing is decoded
// further once a check fails. The full rules and the compatibility policy are
// in docs/share-code-protocol.md; fixtures in tests/fixtures/sharing/v1 freeze
// the format.

#include "core/model.hpp"
#include "core/result.hpp"
#include "sharing/manifest.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::sharing {

inline constexpr int kProtocolVersion = 1;
inline constexpr std::string_view kEnvelopePrefix = "PMM1";
inline constexpr std::size_t kMaxEncodedBytes =
    std::size_t{2} * 1024 * 1024; // raw input, untrimmed
inline constexpr std::size_t kMaxDecompressedBytes = std::size_t{8} * 1024 * 1024; // manifest JSON
// kMaxEntries (10000) is defined in sharing/manifest.hpp.

// Validates the manifest and encodes it canonically: the same manifest always
// yields the same code. Fails with the validation error, or LimitExceeded when
// the result would exceed what decodeShareCode() accepts.
Result<std::string> encodeShareCode(const Manifest& manifest);

// Decodes and fully validates a code. Surrounding ASCII whitespace (space, tab,
// CR, LF) is ignored. Error codes: LimitExceeded, UnsupportedVersion,
// ParseError, IntegrityCheckFailed, ValidationFailed, UnsupportedGame.
Result<Manifest> decodeShareCode(std::string_view input);

// Manifest for exporting `playset`: every entry, disabled ones included, in
// exact order. Fails with WrongGame if an entry belongs to another game. The
// result is validated only when encoded.
Result<Manifest> manifestFromPlayset(const Playset& playset, std::string gameVersionHint);

// Entries for the imported playset, in manifest order, scoped to manifest.game.
std::vector<PlaysetEntry> playsetEntriesFromManifest(const Manifest& manifest);

} // namespace pmm::sharing
