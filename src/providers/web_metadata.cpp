#include "providers/web_metadata.hpp"

#include "providers/workshop_item.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <limits>
#include <type_traits>

namespace pmm::providers {
namespace {

using Json = nlohmann::json;

// Accepts a JSON number or a decimal string; anything else yields fallback.
template <class T>
T numberOr(const Json& obj, const char* key, T fallback) {
    const auto it = obj.find(key);
    if (it == obj.end())
        return fallback;
    if (it->is_number_integer()) {
        if constexpr (std::is_unsigned_v<T>) {
            if (!it->is_number_unsigned())
                return fallback;
            const auto v = it->get<std::uint64_t>();
            if constexpr (sizeof(T) < sizeof(std::uint64_t)) {
                if (v > std::numeric_limits<T>::max())
                    return fallback;
            }
            return static_cast<T>(v);
        } else {
            const auto v = it->get<std::int64_t>();
            if constexpr (sizeof(T) < sizeof(std::int64_t)) {
                if (v < std::numeric_limits<T>::min() || v > std::numeric_limits<T>::max())
                    return fallback;
            }
            return static_cast<T>(v);
        }
    }
    if (it->is_boolean())
        return static_cast<T>(it->get<bool>() ? 1 : 0);
    if (it->is_string()) {
        const auto& s = it->get_ref<const std::string&>();
        T value{};
        const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
        if (ec == std::errc() && end == s.data() + s.size())
            return value;
    }
    return fallback;
}

std::string stringOr(const Json& obj, const char* key) {
    const auto it = obj.find(key);
    if (it == obj.end())
        return {};
    if (it->is_string())
        return it->get<std::string>();
    if (it->is_number_unsigned())
        return std::to_string(it->get<std::uint64_t>());
    if (it->is_number_integer())
        return std::to_string(it->get<std::int64_t>());
    return {};
}

} // namespace

Result<std::string> buildPublishedFileDetailsRequest(std::span<const std::string> ids) {
    if (ids.empty() || ids.size() > kMaxPublishedFileDetailsItems) {
        return makeError(ErrorCode::InvalidArgument,
                         "A Workshop details request needs between 1 and " +
                             std::to_string(kMaxPublishedFileDetailsItems) + " item ids.",
                         std::to_string(ids.size()) + " ids");
    }
    std::string body = "itemcount=" + std::to_string(ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (!isValidPublishedFileId(ids[i])) {
            return makeError(ErrorCode::InvalidArgument,
                             "Cannot request Workshop details: an item id is invalid.",
                             "index " + std::to_string(i));
        }
        body += "&publishedfileids%5B";
        body += std::to_string(i);
        body += "%5D=";
        body += ids[i];
    }
    return body;
}

Result<std::vector<WorkshopItemMetadata>> parsePublishedFileDetailsResponse(
    std::string_view body) {
    if (body.size() > kMaxPublishedFileDetailsResponseBytes) {
        return makeError(ErrorCode::LimitExceeded, "The Workshop details reply is too large.",
                         std::to_string(body.size()) + " bytes");
    }
    Json root;
    try {
        root = Json::parse(body.begin(), body.end(), nullptr, /*allow_exceptions=*/false);
    } catch (const std::exception& e) {
        return makeError(ErrorCode::ParseError, "The Workshop details reply is not valid JSON.",
                         e.what());
    }
    if (root.is_discarded() || !root.is_object())
        return makeError(ErrorCode::ParseError, "The Workshop details reply is not valid JSON.");

    const auto response = root.find("response");
    if (response == root.end() || !response->is_object()) {
        return makeError(ErrorCode::ParseError,
                         "The Workshop details reply has no \"response\" object.");
    }
    const int overall = numberOr<int>(*response, "result", 0);
    const auto details = response->find("publishedfiledetails");
    if (details == response->end() || !details->is_array()) {
        if (overall != 1) {
            return makeError(ErrorCode::ProviderUnavailable,
                             "The Steam Web API rejected the Workshop details request.",
                             "result " + std::to_string(overall));
        }
        return std::vector<WorkshopItemMetadata>{};
    }

    std::vector<WorkshopItemMetadata> items;
    items.reserve(details->size());
    for (const auto& d : *details) {
        if (!d.is_object())
            return makeError(ErrorCode::ParseError, "A Workshop details entry is not an object.");
        WorkshopItemMetadata m;
        m.id = stringOr(d, "publishedfileid");
        if (!isValidPublishedFileId(m.id)) {
            return makeError(ErrorCode::ParseError,
                             "A Workshop details entry has an invalid item id.",
                             "publishedfileid=\"" + m.id.substr(0, 64) + "\"");
        }
        m.resultCode = numberOr<int>(d, "result", 0);
        m.found = m.resultCode == 1;
        if (m.found) {
            m.title = stringOr(d, "title");
            m.consumerAppId = stringOr(d, "consumer_app_id");
            m.creatorAppId = stringOr(d, "creator_app_id");
            m.fileSize = numberOr<std::uint64_t>(d, "file_size", 0);
            m.timeCreated = numberOr<std::int64_t>(d, "time_created", 0);
            m.timeUpdated = numberOr<std::int64_t>(d, "time_updated", 0);
            m.banned = numberOr<int>(d, "banned", 0) != 0;
            m.banReason = stringOr(d, "ban_reason");
            m.visibility = numberOr<int>(d, "visibility", 0);
            if (const auto tags = d.find("tags"); tags != d.end() && tags->is_array()) {
                for (const auto& t : *tags) {
                    if (t.is_object() && t.contains("tag") && t["tag"].is_string())
                        m.tags.push_back(t["tag"].get<std::string>());
                }
            }
        }
        items.push_back(std::move(m));
    }
    return items;
}

Status validateWorkshopItemForGame(const WorkshopItemMetadata& item,
                                   std::string_view expectedAppId) {
    if (item.resultCode == 9) {
        return makeError(ErrorCode::ItemUnavailable,
                         "Workshop item " + item.id +
                             " was not found. It may have been deleted, or it is private or "
                             "friends-only.",
                         "EResult 9 (FileNotFound)");
    }
    if (!item.found) {
        const SteamResultInfo info = classifySteamResult(item.resultCode);
        return makeError(ErrorCode::ItemUnavailable,
                         "Steam returned no details for Workshop item " + item.id + ": " +
                             std::string(info.message) + ".",
                         "EResult " + std::to_string(item.resultCode) + " (" +
                             std::string(info.name) + ")");
    }
    if (item.banned) {
        return makeError(ErrorCode::ItemUnavailable,
                         "Workshop item " + item.id + " (" + item.title +
                             ") has been banned from the Workshop.",
                         item.banReason);
    }
    if (item.consumerAppId != expectedAppId) {
        return makeError(ErrorCode::WrongGame,
                         "Workshop item " + item.id + " (" + item.title +
                             ") belongs to a different game (Steam app " + item.consumerAppId +
                             ").",
                         "expected app " + std::string(expectedAppId));
    }
    return {};
}

} // namespace pmm::providers
