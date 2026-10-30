#pragma once

// Request builder and response parser for the public, key-less Steam Web API
// call ISteamRemoteStorage/GetPublishedFileDetails/v1. It is used to show
// names/sizes for missing Workshop items and to verify that an imported id
// really belongs to the expected game before acquisition starts. The HTTP
// request itself is performed by the application layer (Qt networking); this
// module only builds the form body and parses the JSON reply.
//
// Observed reply shape (2026-10-29): "file_size" is a string, app ids,
// timestamps, "visibility" and "banned" are numbers, and unknown/hidden items
// come back as {"publishedfileid":"…","result":9}. The parser accepts numbers
// and numeric strings for every numeric field to tolerate either form.

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
    bool banned = false;
    std::string banReason;
    int visibility = 0; // WorkshopVisibility value as reported
    std::vector<std::string> tags;
};

// Parses the reply body. Items appear in reply order. Fails with ParseError on
// malformed JSON or a missing "response" object, and LimitExceeded when the
// body is larger than kMaxPublishedFileDetailsResponseBytes.
Result<std::vector<WorkshopItemMetadata>> parsePublishedFileDetailsResponse(std::string_view body);

// Checks that an item can be acquired for the game with Steam AppId
// `expectedAppId`: not found (result 9) -> ItemUnavailable; other non-OK
// result -> ItemUnavailable with the EResult in detail; banned ->
// ItemUnavailable; consumer_app_id mismatch -> WrongGame.
Status validateWorkshopItemForGame(const WorkshopItemMetadata& item,
                                   std::string_view expectedAppId);

} // namespace pmm::providers
