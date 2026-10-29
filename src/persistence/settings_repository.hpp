#pragma once

// Application settings as UTF-8 key/value pairs (theme, window geometry, last
// active playset per game, ...). Typed helpers parse the stored text on read.

#include "core/result.hpp"
#include "persistence/database.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pmm::persistence {

class SettingsRepository {
public:
    explicit SettingsRepository(Database& db) noexcept : m_db(db) {}

    // nullopt when the key is not set.
    Result<std::optional<std::string>> get(std::string_view key);
    Status set(std::string_view key, std::string_view value);
    Status remove(std::string_view key); // succeeds if the key is absent

    // nullopt when unset; ParseError when the stored text is not a valid
    // value. Booleans are stored as "true"/"false" ("1"/"0" are accepted).
    Result<std::optional<std::int64_t>> getInt(std::string_view key);
    Result<std::optional<bool>> getBool(std::string_view key);
    Status setInt(std::string_view key, std::int64_t value);
    Status setBool(std::string_view key, bool value);

private:
    Database& m_db;
};

} // namespace pmm::persistence
