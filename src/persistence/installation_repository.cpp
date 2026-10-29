#include "persistence/installation_repository.hpp"

#include "persistence/internal/mapping.hpp"

namespace pmm::persistence {

using namespace internal;

namespace {

constexpr const char* kSelect =
    "SELECT game_key, install_path, user_data_path, executable_path, default_args_json, "
    "distribution, platform, steam_app_id, steam_library_path, version, version_label, "
    "manually_configured FROM game_installations";

Result<GameInstallation> readInstallation(const Statement& stmt) {
    GameInstallation out;
    out.game = stmt.columnText(0);
    out.installPath = pathFromText(stmt.columnText(1));
    out.userDataPath = pathFromText(stmt.columnText(2));
    out.executablePath = pathFromText(stmt.columnText(3));
    PMM_DB_TRY_VALUE(
        args, stringListFromJson(stmt.columnText(4), "game_installations.default_args_json"));
    out.defaultArguments = std::move(*args);
    const auto distribution = distributionFromText(stmt.columnText(5));
    const auto platform = platformFromText(stmt.columnText(6));
    if (!distribution || !platform)
        return corruptValue("installation of " + out.game, "distribution='" + stmt.columnText(5) +
                                                               "' platform='" + stmt.columnText(6) +
                                                               "'");
    out.distribution = *distribution;
    out.platform = *platform;
    out.steamAppId = stmt.columnText(7);
    out.steamLibraryPath = pathFromText(stmt.columnText(8));
    out.version = stmt.columnText(9);
    out.versionLabel = stmt.columnText(10);
    out.manuallyConfigured = stmt.columnBool(11);
    return out;
}

} // namespace

Status InstallationRepository::upsert(const GameInstallation& installation) {
    if (installation.game.empty())
        return makeError(ErrorCode::InvalidArgument,
                         "Cannot save a game installation without a game key.");
    PMM_TRY(checkStorablePath(installation.installPath, "The game folder"));
    PMM_TRY(checkStorablePath(installation.userDataPath, "The game's user data folder"));
    PMM_TRY(checkStorablePath(installation.executablePath, "The game executable"));
    PMM_TRY(checkStorablePath(installation.steamLibraryPath, "The Steam library folder"));
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(stmt,
                     guard.connection().prepare(
                         "INSERT INTO game_installations (game_key, install_path, user_data_path, "
                         "executable_path, default_args_json, distribution, platform, "
                         "steam_app_id, steam_library_path, version, version_label, "
                         "manually_configured, updated_at) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                         "ON CONFLICT (game_key) DO UPDATE SET "
                         "install_path = excluded.install_path, "
                         "user_data_path = excluded.user_data_path, "
                         "executable_path = excluded.executable_path, "
                         "default_args_json = excluded.default_args_json, "
                         "distribution = excluded.distribution, platform = excluded.platform, "
                         "steam_app_id = excluded.steam_app_id, "
                         "steam_library_path = excluded.steam_library_path, "
                         "version = excluded.version, version_label = excluded.version_label, "
                         "manually_configured = excluded.manually_configured, "
                         "updated_at = excluded.updated_at"));
    stmt->bindAll(installation.game, pathToText(installation.installPath),
                  pathToText(installation.userDataPath), pathToText(installation.executablePath),
                  stringListToJson(installation.defaultArguments),
                  toString(installation.distribution), toString(installation.platform),
                  installation.steamAppId, pathToText(installation.steamLibraryPath),
                  installation.version, installation.versionLabel, installation.manuallyConfigured,
                  m_db.now());
    return stmt->run();
}

Result<std::optional<GameInstallation>> InstallationRepository::get(const GameKey& game) {
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(stmt,
                     guard.connection().prepare(std::string(kSelect) + " WHERE game_key = ?"));
    stmt->bind(1, game);
    PMM_DB_TRY_VALUE(row, stmt->step());
    if (!*row)
        return std::optional<GameInstallation>();
    PMM_DB_TRY_VALUE(installation, readInstallation(*stmt));
    return std::optional<GameInstallation>(std::move(*installation));
}

Result<std::vector<GameInstallation>> InstallationRepository::list() {
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(stmt, guard.connection().prepare(std::string(kSelect) + " ORDER BY game_key"));
    std::vector<GameInstallation> out;
    for (;;) {
        PMM_DB_TRY_VALUE(row, stmt->step());
        if (!*row)
            break;
        PMM_DB_TRY_VALUE(installation, readInstallation(*stmt));
        out.push_back(std::move(*installation));
    }
    return out;
}

Status InstallationRepository::remove(const GameKey& game) {
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(
        stmt, guard.connection().prepare("DELETE FROM game_installations WHERE game_key = ?"));
    return stmt->bind(1, game).run();
}

} // namespace pmm::persistence
