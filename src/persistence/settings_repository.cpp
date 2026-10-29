#include "persistence/settings_repository.hpp"

#include "persistence/internal/mapping.hpp"

#include <charconv>

namespace pmm::persistence {

namespace {
Status validateKey(std::string_view key) {
    if (key.empty())
        return makeError(ErrorCode::InvalidArgument, "A setting name must not be empty.");
    return {};
}

Error invalidSetting(std::string_view key, std::string_view expected, const std::string& value) {
    return makeError(ErrorCode::ParseError,
                     "The saved setting '" + std::string(key) + "' is not " +
                         std::string(expected) + "; the default will be used.",
                     "stored value: " + value.substr(0, 80));
}
} // namespace

Result<std::optional<std::string>> SettingsRepository::get(std::string_view key) {
    PMM_TRY(validateKey(key));
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(stmt, guard.connection().prepare("SELECT value FROM settings WHERE key = ?"));
    stmt->bind(1, key);
    PMM_DB_TRY_VALUE(row, stmt->step());
    if (!*row)
        return std::optional<std::string>();
    return std::optional<std::string>(stmt->columnText(0));
}

Status SettingsRepository::set(std::string_view key, std::string_view value) {
    PMM_TRY(validateKey(key));
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(
        stmt, guard.connection().prepare("INSERT INTO settings (key, value) VALUES (?, ?) "
                                         "ON CONFLICT (key) DO UPDATE SET value = excluded.value"));
    return stmt->bindAll(key, value).run();
}

Status SettingsRepository::remove(std::string_view key) {
    PMM_TRY(validateKey(key));
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(stmt, guard.connection().prepare("DELETE FROM settings WHERE key = ?"));
    return stmt->bind(1, key).run();
}

Result<std::optional<std::int64_t>> SettingsRepository::getInt(std::string_view key) {
    PMM_DB_TRY_VALUE(text, get(key));
    if (!text->has_value())
        return std::optional<std::int64_t>();
    const std::string& value = **text;
    std::int64_t parsed = 0;
    const auto* end = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(value.data(), end, parsed);
    if (value.empty() || ec != std::errc() || ptr != end)
        return invalidSetting(key, "a whole number", value);
    return std::optional<std::int64_t>(parsed);
}

Result<std::optional<bool>> SettingsRepository::getBool(std::string_view key) {
    PMM_DB_TRY_VALUE(text, get(key));
    if (!text->has_value())
        return std::optional<bool>();
    const std::string& value = **text;
    if (value == "true" || value == "1")
        return std::optional<bool>(true);
    if (value == "false" || value == "0")
        return std::optional<bool>(false);
    return invalidSetting(key, "true or false", value);
}

Status SettingsRepository::setInt(std::string_view key, std::int64_t value) {
    return set(key, std::to_string(value));
}

Status SettingsRepository::setBool(std::string_view key, bool value) {
    return set(key, value ? "true" : "false");
}

} // namespace pmm::persistence
