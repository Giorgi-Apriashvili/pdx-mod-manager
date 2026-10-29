#include "persistence/internal/mapping.hpp"

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

std::optional<OsPlatform> platformFromText(std::string_view text) noexcept {
    for (auto value : {OsPlatform::Windows, OsPlatform::Linux, OsPlatform::MacOS}) {
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

std::string pathToText(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    return {reinterpret_cast<const char*>(u8.data()), u8.size()};
}

std::filesystem::path pathFromText(std::string_view utf8) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
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
