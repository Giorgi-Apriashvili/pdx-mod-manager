#include "providers/workshop_item.hpp"

#include <array>
#include <limits>

namespace pmm::providers {
namespace {

template <class T> std::optional<T> parseCanonicalDecimal(std::string_view text) noexcept {
    constexpr std::size_t kMaxDigits =
        static_cast<std::size_t>(std::numeric_limits<T>::digits10) + 1;
    if (text.empty() || text.size() > kMaxDigits)
        return std::nullopt;
    if (text.front() == '0') // rejects "0" and leading zeros
        return std::nullopt;
    T value = 0;
    for (char c : text) {
        if (c < '0' || c > '9')
            return std::nullopt;
        const T digit = static_cast<T>(c - '0');
        if (value > (std::numeric_limits<T>::max() - digit) / 10)
            return std::nullopt;
        value = static_cast<T>(value * 10 + digit);
    }
    return value;
}

using D = FailureDisposition;

struct ResultRow {
    int value;
    std::string_view name;
    ErrorCode code;
    FailureDisposition disposition;
    std::string_view message;
};

// Subset of EResult that ISteamUGC / ISteamRemoteStorage calls are known or
// likely to return. Everything else falls back to kUnknown.
constexpr std::array<ResultRow, 37> kResults{{
    {1, "OK", ErrorCode::Internal, D::None, "Steam accepted the request"},
    {2, "Fail", ErrorCode::ProviderUnavailable, D::Retryable, "Steam reported a generic failure"},
    {3, "NoConnection", ErrorCode::Offline, D::Retryable, "Steam has no connection to its servers"},
    {8, "InvalidParam", ErrorCode::ItemUnavailable, D::Unavailable,
     "Steam rejected the Workshop item id as invalid"},
    {9, "FileNotFound", ErrorCode::ItemUnavailable, D::Unavailable,
     "The Workshop item does not exist or was removed"},
    {10, "Busy", ErrorCode::Busy, D::Retryable, "Steam is busy"},
    {11, "InvalidState", ErrorCode::ProviderUnavailable, D::Retryable,
     "Steam is not ready to handle the request"},
    {15, "AccessDenied", ErrorCode::AccessDenied, D::Unavailable,
     "Access to the Workshop item was denied (it may be private, friends-only or hidden)"},
    {16, "Timeout", ErrorCode::Timeout, D::Retryable, "The request to Steam timed out"},
    {17, "Banned", ErrorCode::AccessDenied, D::Unavailable,
     "The Workshop item or account is banned from the Workshop"},
    {20, "ServiceUnavailable", ErrorCode::ProviderUnavailable, D::Retryable,
     "The Steam Workshop service is temporarily unavailable"},
    {21, "NotLoggedOn", ErrorCode::ProviderUnavailable, D::UserAction,
     "Steam is not signed in; sign in to Steam (online mode) and retry"},
    {22, "Pending", ErrorCode::Busy, D::Retryable, "Steam is still processing the request"},
    {24, "InsufficientPrivilege", ErrorCode::AccessDenied, D::UserAction,
     "The Steam account is not allowed to use the Workshop (limited account or missing licence)"},
    {25, "LimitExceeded", ErrorCode::LimitExceeded, D::UserAction,
     "Steam reports a limit was exceeded (for example too many subscriptions)"},
    {26, "Revoked", ErrorCode::ItemUnavailable, D::Unavailable, "Access to the item was revoked"},
    {27, "Expired", ErrorCode::ItemUnavailable, D::Unavailable, "The item has expired"},
    {29, "DuplicateRequest", ErrorCode::Busy, D::Retryable,
     "An identical request is already in progress"},
    {35, "ConnectFailed", ErrorCode::Offline, D::Retryable,
     "Steam could not connect to its servers"},
    {37, "IOFailure", ErrorCode::IoError, D::Retryable,
     "Steam reported a network or disk I/O failure"},
    {38, "RemoteDisconnect", ErrorCode::Offline, D::Retryable, "Steam lost its server connection"},
    {40, "Blocked", ErrorCode::AccessDenied, D::Unavailable,
     "The item's author has blocked access"},
    {44, "ServiceReadOnly", ErrorCode::ProviderUnavailable, D::Retryable,
     "The Steam Workshop is temporarily read-only"},
    {48, "TryAnotherCM", ErrorCode::Offline, D::Retryable, "Steam is switching servers"},
    {52, "Cancelled", ErrorCode::Cancelled, D::Retryable, "Steam cancelled the request"},
    {53, "DataCorruption", ErrorCode::IntegrityCheckFailed, D::Retryable,
     "Steam detected corrupted data"},
    {54, "DiskFull", ErrorCode::IoError, D::UserAction,
     "The disk holding the Steam library is full; free space and retry"},
    {55, "RemoteCallFailed", ErrorCode::ProviderUnavailable, D::Retryable,
     "A Steam server call failed"},
    {69, "ParentalControlRestricted", ErrorCode::AccessDenied, D::UserAction,
     "Steam Family View or parental controls block the Workshop"},
    {78, "ValueOutOfRange", ErrorCode::ItemUnavailable, D::Unavailable,
     "Steam rejected the Workshop item id as out of range"},
    {79, "UnexpectedError", ErrorCode::ProviderUnavailable, D::Retryable,
     "Steam reported an unexpected error"},
    {80, "Disabled", ErrorCode::ProviderUnavailable, D::UserAction,
     "The Workshop feature is disabled for this account or game"},
    {83, "RegionLocked", ErrorCode::AccessDenied, D::Unavailable,
     "The item is not available in your region"},
    {84, "RateLimitExceeded", ErrorCode::Busy, D::Retryable,
     "Steam rate-limited the requests; wait a little and retry"},
    {86, "ItemDeleted", ErrorCode::ItemUnavailable, D::Unavailable,
     "The Workshop item was deleted by its author"},
    {104, "InvalidItemType", ErrorCode::ItemUnavailable, D::Unavailable,
     "The id does not refer to a Workshop item that can be subscribed"},
    {128, "NotSupported", ErrorCode::NotImplemented, D::Unavailable,
     "Steam does not support this operation for the item"},
}};

constexpr ResultRow kUnknown{0, "Unknown", ErrorCode::ProviderUnavailable, D::Retryable,
                             "Steam reported an unrecognised error"};

} // namespace

ItemFlags decodeItemFlags(std::uint32_t raw) noexcept {
    ItemFlags f;
    f.raw = raw;
    f.subscribed = (raw & item_flag::Subscribed) != 0;
    f.legacy = (raw & item_flag::LegacyItem) != 0;
    f.installed = (raw & item_flag::Installed) != 0;
    f.needsUpdate = (raw & item_flag::NeedsUpdate) != 0;
    f.downloading = (raw & item_flag::Downloading) != 0;
    f.downloadPending = (raw & item_flag::DownloadPending) != 0;
    return f;
}

std::string describeItemFlags(std::uint32_t raw) {
    static constexpr std::array<std::pair<std::uint32_t, std::string_view>, 6> kNames{{
        {item_flag::Subscribed, "subscribed"},
        {item_flag::LegacyItem, "legacy"},
        {item_flag::Installed, "installed"},
        {item_flag::NeedsUpdate, "needs_update"},
        {item_flag::Downloading, "downloading"},
        {item_flag::DownloadPending, "download_pending"},
    }};
    std::string out;
    std::uint32_t known = 0;
    for (const auto& [bit, name] : kNames) {
        known |= bit;
        if ((raw & bit) == 0)
            continue;
        if (!out.empty())
            out += '|';
        out += name;
    }
    if (const std::uint32_t unknown = raw & ~known; unknown != 0) {
        static constexpr char kHex[] = "0123456789abcdef";
        std::string hex;
        for (std::uint32_t v = unknown; v != 0; v >>= 4)
            hex.insert(hex.begin(), kHex[v & 0xf]);
        if (!out.empty())
            out += '|';
        out += "0x" + hex;
    }
    return out.empty() ? std::string("none") : out;
}

bool isValidPublishedFileId(std::string_view id) noexcept {
    return parsePublishedFileId(id).has_value();
}

std::optional<std::uint64_t> parsePublishedFileId(std::string_view id) noexcept {
    return parseCanonicalDecimal<std::uint64_t>(id);
}

bool isValidSteamAppId(std::string_view appId) noexcept {
    return parseSteamAppId(appId).has_value();
}

std::optional<std::uint32_t> parseSteamAppId(std::string_view appId) noexcept {
    return parseCanonicalDecimal<std::uint32_t>(appId);
}

const char* toString(FailureDisposition disposition) noexcept {
    switch (disposition) {
    case FailureDisposition::None:
        return "none";
    case FailureDisposition::Retryable:
        return "retryable";
    case FailureDisposition::UserAction:
        return "user_action";
    case FailureDisposition::Unavailable:
        return "unavailable";
    }
    return "retryable";
}

SteamResultInfo classifySteamResult(int eresult) noexcept {
    const ResultRow* row = &kUnknown;
    for (const auto& r : kResults) {
        if (r.value == eresult) {
            row = &r;
            break;
        }
    }
    SteamResultInfo info;
    info.value = eresult;
    info.name = row->name;
    info.code = row->code;
    info.disposition = row->disposition;
    info.message = row->message;
    return info;
}

Error steamResultError(int eresult, std::string_view context) {
    const SteamResultInfo info = classifySteamResult(eresult);
    std::string message(context);
    message += " failed: ";
    message += info.message;
    message += '.';
    std::string detail = "EResult ";
    detail += std::to_string(eresult);
    detail += " (";
    detail += info.name;
    detail += ')';
    return makeError(info.code, std::move(message), std::move(detail));
}

} // namespace pmm::providers
