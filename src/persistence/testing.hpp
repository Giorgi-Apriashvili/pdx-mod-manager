#pragma once

// Test-only hooks for the persistence layer. Production code must not use
// these: they bypass the built-in migration list and the system clock.

#include "persistence/database.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace pmm::persistence::testing {

// Opens `file` with an explicit migration list instead of the built-in one,
// e.g. a prefix of builtinMigrations() to create an old database, or the
// built-ins plus a fake future migration to exercise the upgrade path.
Result<std::unique_ptr<Database>> openWithMigrations(const std::filesystem::path& file,
                                                     const std::vector<Migration>& migrations);

// Built-in migrations followed by `extra` (versions must continue the sequence).
Result<std::unique_ptr<Database>> openWithExtraMigrations(const std::filesystem::path& file,
                                                          const std::vector<Migration>& extra);

Result<std::unique_ptr<Database>>
openInMemoryWithMigrations(const std::vector<Migration>& migrations);

// Replaces the clock used for stored timestamps. Set it before sharing the
// Database between threads; pass an empty function to restore the system clock.
void setClock(Database& db, std::function<std::int64_t()> clock);

} // namespace pmm::persistence::testing
