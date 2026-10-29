#include "providers/fallback.hpp"

#include "providers/workshop_item.hpp"

namespace pmm::providers {
namespace {

Status checkId(std::string_view id) {
    if (isValidPublishedFileId(id))
        return {};
    return makeError(ErrorCode::InvalidArgument,
                     "Cannot build a Workshop link: the item id is not a valid Workshop id.",
                     "id=\"" + std::string(id.substr(0, 64)) + "\"");
}

} // namespace

Result<std::string> steamClientItemUrl(std::string_view publishedFileId) {
    PMM_TRY(checkId(publishedFileId));
    return "steam://url/CommunityFilePage/" + std::string(publishedFileId);
}

Result<std::string> workshopWebItemUrl(std::string_view publishedFileId) {
    PMM_TRY(checkId(publishedFileId));
    return "https://steamcommunity.com/sharedfiles/filedetails/?id=" +
           std::string(publishedFileId);
}

Result<ManualFallbackLinks> manualFallbackLinks(std::string_view publishedFileId) {
    PMM_TRY(checkId(publishedFileId));
    ManualFallbackLinks links;
    links.steamClientUrl = *steamClientItemUrl(publishedFileId);
    links.webUrl = *workshopWebItemUrl(publishedFileId);
    return links;
}

} // namespace pmm::providers
