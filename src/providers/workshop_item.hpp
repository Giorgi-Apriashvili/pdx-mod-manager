#pragma once

// Steam Workshop item vocabulary shared by the helper protocol and the
// acquisition state machine: EItemState flag decoding, published-file id
// validation and EResult classification. Pure functions, no I/O.
//
// Values are taken from the public Steamworks documentation (ISteamUGC
// EItemState, steam_api EResult). The Steamworks SDK is not used or vendored.

#include "core/result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pmm::providers {

// EItemState bit flags as returned by ISteamUGC::GetItemState.
namespace item_flag {
inline constexpr std::uint32_t None = 0;
inline constexpr std::uint32_t Subscribed = 1;       // current user is subscribed
inline constexpr std::uint32_t LegacyItem = 2;       // single-file item from the old Workshop
inline constexpr std::uint32_t Installed = 4;        // content present locally
inline constexpr std::uint32_t NeedsUpdate = 8;      // installed but a newer version exists
inline constexpr std::uint32_t Downloading = 16;     // download in progress
inline constexpr std::uint32_t DownloadPending = 32; // queued, not started yet
} // namespace item_flag

struct ItemFlags {
    std::uint32_t raw = 0;
    bool subscribed = false;
    bool legacy = false;
    bool installed = false;
    bool needsUpdate = false;
    bool downloading = false;
    bool downloadPending = false;

    // Installed, current and not being (re)downloaded. Content must still be
    // verified on disk before the item counts as ready.
    bool installedAndCurrent() const noexcept {
        return installed && !needsUpdate && !downloading && !downloadPending;
    }
};

ItemFlags decodeItemFlags(std::uint32_t raw) noexcept;

// "subscribed|installed", "none" when no flag is set; unknown bits as "0x..".
std::string describeItemFlags(std::uint32_t raw);

// Snapshot of one item as reported by the provider (helper "state" event).
struct ItemStatus {
    std::string id;
    std::uint32_t flags = 0;
    std::uint64_t bytesDownloaded = 0;
    std::uint64_t bytesTotal = 0;
    std::string installPath; // UTF-8, as reported by Steam; empty when unknown
    std::uint64_t sizeOnDisk = 0;
    std::uint32_t timestamp = 0; // unix seconds of the installed version

    bool operator==(const ItemStatus&) const = default;
};

// Published file ids are canonical decimal uint64 strings: 1-20 digits, no
// sign, no leading zero, non-zero and <= 2^64-1. Canonical form matters
// because ids are compared as strings everywhere (identities, folder names).
bool isValidPublishedFileId(std::string_view id) noexcept;
std::optional<std::uint64_t> parsePublishedFileId(std::string_view id) noexcept;

// Steam AppIds: canonical decimal, non-zero, fits in uint32.
bool isValidSteamAppId(std::string_view appId) noexcept;
std::optional<std::uint32_t> parseSteamAppId(std::string_view appId) noexcept;

// How the acquisition flow reacts to a non-OK EResult.
enum class FailureDisposition {
    None,        // success
    Retryable,   // transient: offline, timeout, busy, service hiccup
    UserAction,  // the user must do something (log in, free disk space, ...)
    Unavailable, // the item cannot be acquired (deleted, private, banned, invalid)
};
const char* toString(FailureDisposition disposition) noexcept;

struct SteamResultInfo {
    int value = 0;
    std::string_view name; // "OK", "FileNotFound", ... or "Unknown"
    ErrorCode code = ErrorCode::Internal;
    FailureDisposition disposition = FailureDisposition::Retryable;
    std::string_view message; // user-facing explanation, no trailing period

    bool ok() const noexcept { return disposition == FailureDisposition::None; }
    bool retryable() const noexcept { return disposition == FailureDisposition::Retryable; }
};

// Classifies an EResult returned by a Steam call (e.g. SubscribeItem's call
// result). Unknown values are treated as retryable provider errors. The full
// table is documented in docs/steam-workshop.md.
SteamResultInfo classifySteamResult(int eresult) noexcept;

// Error for a failed Steam call; `context` names the operation ("Subscribing
// to Workshop item 123"). Technical detail carries the numeric EResult.
Error steamResultError(int eresult, std::string_view context);

} // namespace pmm::providers
