#include "persistence/inventory_repository.hpp"

#include "persistence/internal/mapping.hpp"
#include "persistence/internal/utf8.hpp"

#include <limits>
#include <set>
#include <utility>

namespace pmm::persistence {

using namespace internal;

namespace {

constexpr const char* kSelect =
    "SELECT game_key, source, provider_id, name, version, supported_version, picture, "
    "declared_path, archive, remote_file_id, tags_json, dependencies_json, extra_json, "
    "content_path, descriptor_path, descriptor_ref, state, problems_json, content_updated_at, "
    "fingerprint FROM installed_mods";

constexpr std::int64_t kMaxSeconds = std::numeric_limits<std::int64_t>::max() / 1000;

std::int64_t secondsToMs(std::int64_t seconds) noexcept {
    // Out-of-range values cannot be real timestamps; store "unknown".
    if (seconds > kMaxSeconds || seconds < -kMaxSeconds)
        return 0;
    return seconds * 1000;
}

Result<InstalledMod> readMod(const Statement& stmt) {
    InstalledMod mod;
    mod.identity = ModIdentity{stmt.columnText(0), stmt.columnText(1), stmt.columnText(2)};
    const std::string what = "inventory entry " + toString(mod.identity);
    ModDescriptor& d = mod.descriptor;
    d.name = stmt.columnText(3);
    d.version = stmt.columnText(4);
    d.supportedVersion = stmt.columnText(5);
    d.picture = stmt.columnText(6);
    d.path = stmt.columnText(7);
    d.archive = stmt.columnText(8);
    d.remoteFileId = stmt.columnText(9);
    PMM_DB_TRY_VALUE(tags, stringListFromJson(stmt.columnText(10), what));
    PMM_DB_TRY_VALUE(dependencies, stringListFromJson(stmt.columnText(11), what));
    PMM_DB_TRY_VALUE(extra, pairListFromJson(stmt.columnText(12), what));
    d.tags = std::move(*tags);
    d.dependencies = std::move(*dependencies);
    d.extra = std::move(*extra);
    mod.contentPath = pathFromText(stmt.columnText(13));
    mod.descriptorPath = pathFromText(stmt.columnText(14));
    mod.descriptorRef = stmt.columnText(15);
    const auto state = installStateFromText(stmt.columnText(16));
    if (!state)
        return corruptValue(what, "state='" + stmt.columnText(16) + "'");
    mod.state = *state;
    PMM_DB_TRY_VALUE(problems, stringListFromJson(stmt.columnText(17), what));
    mod.problems = std::move(*problems);
    mod.contentUpdatedAt = stmt.columnInt64(18) / 1000;
    mod.fingerprint = stmt.columnText(19);
    return mod;
}

Status validateMods(const GameKey& game, const std::vector<InstalledMod>& mods) {
    if (game.empty())
        return makeError(ErrorCode::InvalidArgument, "Cannot save an inventory without a game.");
    std::set<std::pair<std::string_view, std::string_view>> seen;
    for (const auto& mod : mods) {
        const ModIdentity& id = mod.identity;
        if (id.game != game)
            return makeError(ErrorCode::InvalidArgument,
                             "A scanned mod belongs to a different game than the inventory "
                             "being saved.",
                             toString(id) + " in inventory of " + game);
        if (id.source.empty() || id.id.empty())
            return makeError(ErrorCode::InvalidArgument,
                             "A scanned mod has no provider or provider id.", toString(id));
        if (!seen.emplace(id.source, id.id).second)
            return makeError(ErrorCode::InvalidArgument,
                             "The same mod was found twice in one scan.", toString(id));
        const std::string label =
            sanitizeUtf8(mod.descriptor.name.empty() ? id.id : mod.descriptor.name);
        PMM_TRY(checkStorablePath(mod.contentPath, "The folder of the mod '" + label + "'"));
        PMM_TRY(checkStorablePath(mod.descriptorPath,
                                  "The descriptor file of the mod '" + label + "'"));
    }
    return {};
}

} // namespace

Status InventoryRepository::replaceForGame(const GameKey& game,
                                           const std::vector<InstalledMod>& mods) {
    PMM_TRY(validateMods(game, mods));
    auto guard = m_db.access();
    Connection& connection = guard.connection();
    return m_db.transaction([&]() -> Status {
        PMM_DB_TRY_VALUE(clear,
                         connection.prepare("DELETE FROM installed_mods WHERE game_key = ?"));
        PMM_TRY(clear->bind(1, game).run());
        PMM_DB_TRY_VALUE(
            insert,
            connection.prepare(
                "INSERT INTO installed_mods (game_key, source, provider_id, name, version, "
                "supported_version, picture, declared_path, archive, remote_file_id, tags_json, "
                "dependencies_json, extra_json, content_path, descriptor_path, descriptor_ref, "
                "state, problems_json, content_updated_at, fingerprint, scanned_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        const std::int64_t scannedAt = m_db.now();
        for (const auto& mod : mods) {
            const ModDescriptor& d = mod.descriptor;
            insert->bindAll(mod.identity.game, mod.identity.source, mod.identity.id, d.name,
                            d.version, d.supportedVersion, d.picture, d.path, d.archive,
                            d.remoteFileId, stringListToJson(d.tags),
                            stringListToJson(d.dependencies), pairListToJson(d.extra),
                            pathToText(mod.contentPath), pathToText(mod.descriptorPath),
                            mod.descriptorRef, toString(mod.state), stringListToJson(mod.problems),
                            secondsToMs(mod.contentUpdatedAt), mod.fingerprint, scannedAt);
            PMM_TRY(insert->run());
            insert->reset();
        }
        return {};
    });
}

Result<std::vector<InstalledMod>> InventoryRepository::list(const GameKey& game) {
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(stmt, guard.connection().prepare(std::string(kSelect) +
                                                      " WHERE game_key = ? "
                                                      "ORDER BY source, provider_id"));
    stmt->bind(1, game);
    std::vector<InstalledMod> out;
    for (;;) {
        PMM_DB_TRY_VALUE(row, stmt->step());
        if (!*row)
            break;
        PMM_DB_TRY_VALUE(mod, readMod(*stmt));
        out.push_back(std::move(*mod));
    }
    return out;
}

Result<std::optional<InstalledMod>> InventoryRepository::find(const ModIdentity& identity) {
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(
        stmt, guard.connection().prepare(std::string(kSelect) +
                                         " WHERE game_key = ? AND source = ? AND provider_id = ?"));
    stmt->bindAll(identity.game, identity.source, identity.id);
    PMM_DB_TRY_VALUE(row, stmt->step());
    if (!*row)
        return std::optional<InstalledMod>();
    PMM_DB_TRY_VALUE(mod, readMod(*stmt));
    return std::optional<InstalledMod>(std::move(*mod));
}

} // namespace pmm::persistence
