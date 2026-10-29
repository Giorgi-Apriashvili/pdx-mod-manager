#pragma once

// Manual Workshop fallback: links that open an item's page in the Steam client
// or a browser so the user can subscribe by hand, after which a rescan picks
// the content up. This is a clearly labelled, limited capability; it does NOT
// satisfy automatic acquisition and the UI must say so.
//
// URLs are built only from validated canonical published-file ids, never from
// imported text, so a share code can never smuggle an arbitrary URL in.

#include "core/result.hpp"

#include <string>
#include <string_view>

namespace pmm::providers {

// Label for the UI action; states plainly that nothing is downloaded for the user.
inline constexpr std::string_view kManualFallbackLabel =
    "Open in Steam (manual - subscribe there, then rescan)";

// steam://url/CommunityFilePage/<id>
Result<std::string> steamClientItemUrl(std::string_view publishedFileId);

// https://steamcommunity.com/sharedfiles/filedetails/?id=<id>
Result<std::string> workshopWebItemUrl(std::string_view publishedFileId);

struct ManualFallbackLinks {
    std::string steamClientUrl; // preferred: opens the page inside the Steam client
    std::string webUrl;         // fallback when no steam:// handler is registered
};

Result<ManualFallbackLinks> manualFallbackLinks(std::string_view publishedFileId);

} // namespace pmm::providers
