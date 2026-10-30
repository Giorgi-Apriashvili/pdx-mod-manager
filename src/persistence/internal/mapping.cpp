#include "persistence/internal/mapping.hpp"

#include "persistence/internal/utf8.hpp"

#include <nlohmann/json.hpp>

namespace pmm::persistence::internal {

namespace {
using nlohmann::json;

std::string dumpJson(const json& value) {
    return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

Result<json> parseJson(std::string_view text, std::string_view column) {
    json parsed = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded())
        return corruptValue(column, "invalid JSON: " + std::string(text.substr(0, 120)));
    return parsed;
}
} // namespace

std::optional<DistributionSource> distributionFromText(std::string_view text) noexcept {
    for (auto value :
         {DistributionSource::Steam, DistributionSource::Manual, DistributionSource::Unknown}) {
        if (text == toString(value))
            return value;
    }
    return std::nullopt;
}

std::optional<InstallState> installStateFromText(std::string_view text) noexcept {
    for (auto value : {InstallState::Ready, InstallState::NeedsUpdate, InstallState::Downloading,
                       InstallState::Broken}) {
        if (text == toString(value))
            return value;
    }
    return std::nullopt;
}

// path::u8string() and the char8_t constructor throw on Windows for text
// that is not valid UTF-16 / UTF-8, hence the hand-written conversions.
std::string pathToText(const std::filesystem::path& path) {
    return utf16ToUtf8(std::wstring_view(path.native()));
}

std::filesystem::path pathFromText(std::string_view text) {
    return std::filesystem::path(utf8ToWide(text));
}

bool isStorablePath(const std::filesystem::path& path) noexcept {
    return isValidUtf16(std::wstring_view(path.native()));
}

Status checkStorablePath(const std::filesystem::path& path, std::string_view what) {
    if (isStorablePath(path))
        return {};
    return makeError(ErrorCode::InvalidArgument,
                     std::string(what) +
                         " has a name that contains an invalid character, so it cannot be saved. "
                         "Rename it, then try again.",
                     pathToText(path) + " (contains an unpaired UTF-16 surrogate)");
}

std::string stringListToJson(const std::vector<std::string>& values) {
    return dumpJson(json(values));
}

Result<std::vector<std::string>> stringListFromJson(std::string_view text,
                                                    std::string_view column) {
    auto parsed = parseJson(text, column);
    if (!parsed)
        return std::move(parsed).error();
    if (!parsed->is_array())
        return corruptValue(column, "expected a JSON array");
    std::vector<std::string> out;
    out.reserve(parsed->size());
    for (const auto& item : *parsed) {
        if (!item.is_string())
            return corruptValue(column, "expected an array of strings");
        out.push_back(item.get<std::string>());
    }
    return out;
}

std::string pairListToJson(const std::vector<std::pair<std::string, std::string>>& values) {
    json array = json::array();
    for (const auto& [key, value] : values)
        array.push_back(json::array({key, value}));
    return dumpJson(array);
}

Result<std::vector<std::pair<std::string, std::string>>> pairListFromJson(std::string_view text,
                                                                          std::string_view column) {
    auto parsed = parseJson(text, column);
    if (!parsed)
        return std::move(parsed).error();
    if (!parsed->is_array())
        return corruptValue(column, "expected a JSON array");
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(parsed->size());
    for (const auto& item : *parsed) {
        if (!item.is_array() || item.size() != 2 || !item[0].is_string() || !item[1].is_string())
            return corruptValue(column, "expected an array of [key, value] string pairs");
        out.emplace_back(item[0].get<std::string>(), item[1].get<std::string>());
    }
    return out;
}

Error corruptValue(std::string_view what, std::string detail) {
    std::string message = "Stored data in the application database is damaged (" +
                          std::string(what) +
                          "). Rescan the affected game, or restore a database backup if the "
                          "problem persists.";
    return makeError(ErrorCode::DatabaseError, std::move(message), std::move(detail));
}

} // namespace pmm::persistence::internal
