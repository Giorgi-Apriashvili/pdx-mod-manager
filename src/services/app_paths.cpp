#include "services/app_paths.hpp"

#include "persistence/database.hpp"
#include "platform/fs.hpp"
#include "platform/paths.hpp"

namespace pmm::services {

namespace {
constexpr const char* kTransactionsFolder = "transactions";
}

AppPaths AppPaths::standard() {
    const platform::StandardDirectories dirs = platform::standardDirectories();
    AppPaths paths;
    paths.dataDir = dirs.appData;
    paths.databaseFile = dirs.appData / persistence::Database::kDefaultFileName;
    paths.transactionsDir = dirs.appData / kTransactionsFolder;
    paths.logsDir = dirs.logs;
    paths.cacheDir = dirs.cache;
    return paths;
}

AppPaths AppPaths::fromProfileDir(const std::filesystem::path& profileDir) {
    AppPaths paths;
    paths.dataDir = profileDir;
    paths.databaseFile = profileDir / persistence::Database::kDefaultFileName;
    paths.transactionsDir = profileDir / kTransactionsFolder;
    paths.logsDir = profileDir / "logs";
    paths.cacheDir = profileDir / "cache";
    return paths;
}

Status AppPaths::createDirectories() const {
    for (const auto* dir : {&dataDir, &transactionsDir, &logsDir, &cacheDir}) {
        if (dir->empty())
            continue;
        PMM_TRY(platform::createDirectories(*dir));
    }
    return {};
}

} // namespace pmm::services
