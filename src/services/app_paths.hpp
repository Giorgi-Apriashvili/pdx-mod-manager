#pragma once

// Where the application keeps its own files. standard() uses the per-user OS
// locations (platform::standardDirectories()); fromProfileDir() puts
// everything under one folder, for a clean, isolated profile (tests, and the
// "import into a clean application profile" acceptance procedure).

#include "core/result.hpp"

#include <filesystem>

namespace pmm::services {

struct AppPaths {
    std::filesystem::path dataDir;         // application-owned data root
    std::filesystem::path databaseFile;    // <dataDir>/state.sqlite
    std::filesystem::path transactionsDir; // configuration-write journals and backups
    std::filesystem::path logsDir;
    std::filesystem::path cacheDir;

    static AppPaths standard();
    static AppPaths fromProfileDir(const std::filesystem::path& profileDir);

    // Creates every directory (not the database file). Succeeds if they exist.
    Status createDirectories() const;
};

} // namespace pmm::services
