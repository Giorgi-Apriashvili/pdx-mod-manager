#include "sharing/share_code.hpp"

#include "core/sha256.hpp"
#include "sharing/codec.hpp"
#include "sharing/utf8.hpp"

#include <utility>

namespace pmm::sharing {
namespace {

constexpr std::size_t kDigestHexLength = 64;

bool isAsciiSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view trimAsciiWhitespace(std::string_view text) noexcept {
    while (!text.empty() && isAsciiSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isAsciiSpace(text.back()))
        text.remove_suffix(1);
    return text;
}

bool isDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

// Classifies text that does not start with "PMM1.": another protocol version
// ("PMM<n>.") or not a share code at all.
Error envelopeError(std::string_view code) {
    if (code.substr(0, 3) == "PMM") {
        std::size_t end = 3;
        while (end < code.size() && isDigit(code[end]))
            ++end;
        const std::string_view digits = code.substr(3, end - 3);
        const bool canonicalNumber = digits.size() == 1 || (!digits.empty() && digits.front() != '0');
        if (canonicalNumber && end < code.size() && code[end] == '.') {
            const std::string version = displaySnippet(digits, 12);
            return makeError(ErrorCode::UnsupportedVersion,
                             "This share code uses protocol version " + version +
                                 ", which this version of Paradox Mod Manager does not support "
                                 "(it reads version 1). It was probably created by a newer "
                                 "version; update the app and try again.",
                             "envelope PMM" + version);
        }
    }
    return makeError(ErrorCode::ParseError,
                     "This text is not a Paradox Mod Manager share code. Share codes start with "
                     "\"PMM1.\"; make sure the whole code was copied.");
}

Error damaged(std::string_view what, std::string detail) {
    return makeError(ErrorCode::IntegrityCheckFailed,
                     "The share code appears truncated or damaged: " + std::string(what) +
                         ". Copy the complete code again.",
                     std::move(detail));
}

} // namespace

Result<std::string> encodeShareCode(const Manifest& manifest) {
    auto json = serializeManifestJson(manifest);
    if (!json)
        return std::move(json).error();
    if (json->size() > kMaxDecompressedBytes)
        return makeError(ErrorCode::LimitExceeded,
                         "This playset is too large to share: its description exceeds 8 MiB.",
                         std::to_string(json->size()) + " bytes of JSON");

    auto compressed = zlibCompress(*json);
    if (!compressed)
        return std::move(compressed).error();

    std::string code;
    code.reserve(kEnvelopePrefix.size() + 2 + compressed->size() * 4 / 3 + 4 + kDigestHexLength);
    code += kEnvelopePrefix;
    code += '.';
    code += base64UrlEncode(*compressed);
    code += '.';
    code += sha256Hex(*compressed);
    if (code.size() > kMaxEncodedBytes)
        return makeError(ErrorCode::LimitExceeded,
                         "This playset is too large to share: its code would exceed 2 MiB.",
                         std::to_string(code.size()) + " bytes encoded");
    return code;
}

Result<Manifest> decodeShareCode(std::string_view input) {
    // The size limit applies to the raw input, before any other processing.
    if (input.size() > kMaxEncodedBytes)
        return makeError(ErrorCode::LimitExceeded,
                         "This text is too large to be a share code (limit 2 MiB).",
                         std::to_string(input.size()) + " bytes");

    const std::string_view code = trimAsciiWhitespace(input);
    const std::string_view prefix = "PMM1.";
    if (code.substr(0, prefix.size()) != prefix)
        return envelopeError(code);

    const std::string_view body = code.substr(prefix.size());
    const std::size_t dot = body.find('.');
    if (dot == std::string_view::npos || body.find('.', dot + 1) != std::string_view::npos)
        return makeError(ErrorCode::ParseError,
                         "The share code is malformed: expected PMM1.<data>.<checksum>. Make sure "
                         "the whole code was copied and nothing was added to it.",
                         "wrong number of '.'-separated parts");
    const std::string_view payload = body.substr(0, dot);
    const std::string_view digest = body.substr(dot + 1);

    if (digest.size() != kDigestHexLength)
        return damaged("its checksum is incomplete",
                       "digest has " + std::to_string(digest.size()) + " characters, expected 64");
    for (const char c : digest) {
        if (!(isDigit(c) || (c >= 'a' && c <= 'f')))
            return damaged("its checksum contains invalid characters",
                           "digest is not lowercase hexadecimal");
    }

    if (payload.empty())
        return makeError(ErrorCode::ParseError, "The share code is malformed: it has no data.");
    auto compressed = base64UrlDecode(payload);
    if (!compressed)
        return std::move(compressed).error();

    const std::string actual = sha256Hex(*compressed);
    if (actual != digest)
        return damaged("its checksum does not match its content",
                       "expected sha256 " + std::string(digest) + ", computed " + actual);

    auto json = zlibInflate(*compressed, kMaxDecompressedBytes);
    if (!json)
        return std::move(json).error();
    return parseManifestJson(*json);
}

Result<Manifest> manifestFromPlayset(const Playset& playset, std::string gameVersionHint) {
    Manifest manifest;
    manifest.game = playset.game;
    manifest.playsetName = playset.name;
    manifest.gameVersionHint = std::move(gameVersionHint);
    manifest.mods.reserve(playset.entries.size());
    for (std::size_t i = 0; i < playset.entries.size(); ++i) {
        const PlaysetEntry& entry = playset.entries[i];
        if (!entry.identity.game.empty() && entry.identity.game != playset.game)
            return makeError(ErrorCode::WrongGame,
                             "Entry #" + std::to_string(i + 1) + " belongs to another game (" +
                                 displaySnippet(entry.identity.game) + ") and cannot be shared "
                                 "with this playset.",
                             toString(entry.identity));
        ManifestEntry out;
        out.source = entry.identity.source;
        out.id = entry.identity.id;
        out.nameHint = entry.nameHint;
        out.enabled = entry.enabled;
        out.versionHint = entry.versionHint;
        out.fingerprint = entry.fingerprint;
        manifest.mods.push_back(std::move(out));
    }
    return manifest;
}

std::vector<PlaysetEntry> playsetEntriesFromManifest(const Manifest& manifest) {
    std::vector<PlaysetEntry> entries;
    entries.reserve(manifest.mods.size());
    for (const ManifestEntry& mod : manifest.mods) {
        PlaysetEntry entry;
        entry.identity = ModIdentity{manifest.game, mod.source, mod.id};
        entry.enabled = mod.enabled;
        entry.nameHint = mod.nameHint;
        entry.versionHint = mod.versionHint;
        entry.fingerprint = mod.fingerprint;
        entries.push_back(std::move(entry));
    }
    return entries;
}

} // namespace pmm::sharing
