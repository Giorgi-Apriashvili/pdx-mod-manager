#pragma once

// Request builder and response parser for the public, key-less Steam Web API
// call ISteamRemoteStorage/GetPublishedFileDetails/v1. It is used to show
// names/sizes for missing Workshop items, to verify that an imported id
// really belongs to the expected game before acquisition starts, and, on the
// user's request, to check installed items for newer Workshop versions
// (compareWithWorkshop()). The HTTP request itself is performed by the
// application layer (Qt networking); this module only builds the form body
// and parses the JSON reply.
//
// Observed reply shape (2026-10-29): "file_size" is a string, app ids,
// timestamps, "visibility" and "banned" are numbers, and unknown/hidden items
// come back as {"publishedfileid":"…","result":9}. The parser accepts numbers
// and numeric strings for every numeric field to tolerate either form.

#include "core/model.hpp"
#include "core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::providers {

inline constexpr std::string_view kPublishedFileDetailsUrl =
    "https://api.steampowered.com/ISteamRemoteStorage/GetPublishedFileDetails/v1/";
inline constexpr std::string_view kPublishedFileDetailsContentType =
    "application/x-www-form-urlencoded";
inline constexpr std::size_t kMaxPublishedFileDetailsItems = 100;
inline constexpr std::size_t kMaxPublishedFileDetailsResponseBytes = std::size_t{16} * 1024 * 1024;

// "itemcount=N&publishedfileids%5B0%5D=…" (brackets percent-encoded). Fails on
// an empty list, more than kMaxPublishedFileDetailsItems ids, or any id that
// is not a canonical published-file id.
Result<std::string> buildPublishedFileDetailsRequest(std::span<const std::string> ids);

// Workshop visibility values (ERemoteStoragePublishedFileVisibility).
enum class WorkshopVisibility { Public = 0, FriendsOnly = 1, Private = 2, Unlisted = 3 };

struct WorkshopItemMetadata {
    std::string id;
    bool found = false; // result == 1
    int resultCode = 0; // EResult for this item (9 = not found / not visible)
    std::string title;
    std::string consumerAppId; // game the item is for
    std::string creatorAppId;  // tool/game that uploaded it
    std::uint64_t fileSize = 0;
    std::int64_t timeCreated = 0; // unix seconds
    std::int64_t timeUpdated = 0; // unix seconds
    // "hcontent_file": the id of the current content manifest (very likely
    // what appworkshop_<app>.acf calls "manifest"; not verified). Empty when
    // missing or "0".
    std::string contentManifest;
    bool banned = false;
    std::string banReason;
    int visibility = 0; // WorkshopVisibility value as reported
    std::vector<std::string> tags;
};

// Parses the reply body. Items appear in reply order. Fails with ParseError on
// malformed JSON or a missing "response" object, and LimitExceeded when the
// body is larger than kMaxPublishedFileDetailsResponseBytes.
Result<std::vector<WorkshopItemMetadata>> parsePublishedFileDetailsResponse(std::string_view body);

// What the reply says about an item for the game with Steam AppId
// `expectedAppId`, checked in this order: not found (result 9), another
// non-OK result (no details), banned, a consumer_app_id other than
// `expectedAppId` (another game), else Ok.
enum class WorkshopItemVerdict { Ok, NotFound, NoDetails, Banned, WrongGame };
WorkshopItemVerdict classifyWorkshopItemForGame(const WorkshopItemMetadata& item,
                                                std::string_view expectedAppId) noexcept;

// Checks that an item can be acquired for the game with Steam AppId
// `expectedAppId` (classifyWorkshopItemForGame()): not found (result 9) ->
// ItemUnavailable; other non-OK result -> ItemUnavailable with the EResult in
// detail; banned -> ItemUnavailable; consumer_app_id mismatch -> WrongGame.
Status validateWorkshopItemForGame(const WorkshopItemMetadata& item,
                                   std::string_view expectedAppId);

// How an installed Workshop item compares with the Web API's answer about it.
enum class WorkshopUpdateVerdict {
    UpToDate,        // the Workshop has no version newer than the installed one
    UpdateAvailable, // time_updated is later than the installed version's
    // Not compared: the installed version's time is not Steam's record of it
    // (InstalledMod::contentTimeFromSteam false: the folder's modification
    // time is the local download time), or the reply has no time_updated.
    NotCompared,
    Refused, // the Workshop refuses the item for the game (validateWorkshopItemForGame())
};
const char* toString(WorkshopUpdateVerdict verdict) noexcept; // "up_to_date", ...

struct WorkshopUpdateComparison {
    WorkshopUpdateVerdict verdict = WorkshopUpdateVerdict::NotCompared;
    std::int64_t installedTimeUpdated = 0; // unix seconds; 0 when not Steam's record
    std::int64_t workshopTimeUpdated = 0;  // unix seconds; 0 when unknown
    std::string installedManifest;         // acf manifest; empty when unknown
    std::string workshopManifest;          // hcontent_file; empty when unknown or "0"
    // Refused / NotCompared: why, as a sentence for the user. Refused also
    // says that the installed copy stays.
    std::string message;
    ErrorCode refusal = ErrorCode::ItemUnavailable; // Refused: ItemUnavailable or WrongGame
};

// Compares `installed` (its contentUpdatedAt when it is Steam's record) with
// the Workshop's time_updated for the game with Steam AppId `expectedAppId`.
// Only the times decide: the manifests are recorded, not compared, until it
// is confirmed that hcontent_file and the acf manifest name the same thing.
WorkshopUpdateComparison compareWithWorkshop(const InstalledMod& installed,
                                             const WorkshopItemMetadata& item,
                                             std::string_view expectedAppId);

} // namespace pmm::providers
