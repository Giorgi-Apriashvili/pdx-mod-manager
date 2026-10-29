#include "persistence/playset_repository.hpp"

#include "persistence/internal/mapping.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <set>
#include <utility>

namespace pmm::persistence {

using namespace internal;
using nlohmann::json;

namespace {

constexpr int kSnapshotFormat = 1;

struct PlaysetRow {
    std::int64_t id = 0;
    GameKey game;
    std::string name;
    std::int64_t createdAt = 0;
    std::int64_t updatedAt = 0;
    std::optional<std::int64_t> deletedAt;
};

Error playsetNotFound(std::int64_t id) {
    return makeError(ErrorCode::NotFound,
                     "The playset no longer exists. If it was deleted, it can be restored from "
                     "the playset history.",
                     "playset id " + std::to_string(id));
}

Status validateName(std::string_view name) {
    if (name.find_first_not_of(" \t\r\n") == std::string_view::npos)
        return makeError(ErrorCode::InvalidArgument, "A playset name must not be empty.");
    return {};
}

Status validateEntries(const GameKey& game, const std::vector<PlaysetEntry>& entries) {
    std::set<std::pair<std::string_view, std::string_view>> seen;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const ModIdentity& id = entries[i].identity;
        const std::string where = "entry " + std::to_string(i) + " (" + toString(id) + ")";
        if (!id.game.empty() && id.game != game)
            return makeError(ErrorCode::InvalidArgument,
                             "A mod in the list belongs to a different game than the playset.",
                             where + " in a playset of " + game);
        if (id.source.empty() || id.id.empty())
            return makeError(ErrorCode::InvalidArgument,
                             "A mod in the list has no provider or provider id.", where);
        if (!seen.emplace(id.source, id.id).second) {
            const std::string& label = entries[i].nameHint.empty() ? id.id : entries[i].nameHint;
            return makeError(ErrorCode::InvalidArgument,
                             "The mod '" + label + "' appears more than once in the playset.",
                             where);
        }
    }
    return {};
}

Result<std::optional<PlaysetRow>> loadRow(Connection& c, std::int64_t id) {
    PMM_DB_TRY_VALUE(stmt, c.prepare("SELECT id, game_key, name, created_at, updated_at, "
                                     "deleted_at FROM playsets WHERE id = ?"));
    stmt->bind(1, id);
    PMM_DB_TRY_VALUE(row, stmt->step());
    if (!*row)
        return std::optional<PlaysetRow>();
    PlaysetRow out;
    out.id = stmt->columnInt64(0);
    out.game = stmt->columnText(1);
    out.name = stmt->columnText(2);
    out.createdAt = stmt->columnInt64(3);
    out.updatedAt = stmt->columnInt64(4);
    out.deletedAt = stmt->columnOptionalInt64(5);
    return std::optional<PlaysetRow>(std::move(out));
}

Result<PlaysetRow> loadLiveRow(Connection& c, std::int64_t id) {
    PMM_DB_TRY_VALUE(row, loadRow(c, id));
    if (!row->has_value() || (*row)->deletedAt.has_value())
        return playsetNotFound(id);
    return std::move(**row);
}

Result<std::vector<PlaysetEntry>> loadEntries(Connection& c, std::int64_t id, const GameKey& game) {
    PMM_DB_TRY_VALUE(stmt, c.prepare("SELECT source, provider_id, enabled, name_hint, "
                                     "version_hint, fingerprint FROM playset_entries "
                                     "WHERE playset_id = ? ORDER BY position"));
    stmt->bind(1, id);
    std::vector<PlaysetEntry> out;
    for (;;) {
        PMM_DB_TRY_VALUE(row, stmt->step());
        if (!*row)
            break;
        PlaysetEntry entry;
        entry.identity = ModIdentity{game, stmt->columnText(0), stmt->columnText(1)};
        entry.enabled = stmt->columnBool(2);
        entry.nameHint = stmt->columnText(3);
        entry.versionHint = stmt->columnText(4);
        entry.fingerprint = stmt->columnText(5);
        out.push_back(std::move(entry));
    }
    return out;
}

Result<Playset> loadLivePlayset(Connection& c, std::int64_t id) {
    PMM_DB_TRY_VALUE(row, loadLiveRow(c, id));
    PMM_DB_TRY_VALUE(entries, loadEntries(c, id, row->game));
    Playset out;
    out.id = row->id;
    out.game = std::move(row->game);
    out.name = std::move(row->name);
    out.createdAt = row->createdAt;
    out.updatedAt = row->updatedAt;
    out.entries = std::move(*entries);
    return out;
}

Status writeEntries(Connection& c, std::int64_t id, const std::vector<PlaysetEntry>& entries) {
    PMM_DB_TRY_VALUE(clear, c.prepare("DELETE FROM playset_entries WHERE playset_id = ?"));
    PMM_TRY(clear->bind(1, id).run());
    PMM_DB_TRY_VALUE(insert, c.prepare("INSERT INTO playset_entries (playset_id, position, "
                                       "source, provider_id, enabled, name_hint, version_hint, "
                                       "fingerprint) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const PlaysetEntry& e = entries[i];
        insert->bindAll(id, static_cast<std::int64_t>(i), e.identity.source, e.identity.id,
                        e.enabled, e.nameHint, e.versionHint, e.fingerprint);
        PMM_TRY(insert->run());
        insert->reset();
    }
    return {};
}

Result<std::int64_t> insertPlayset(Connection& c, const GameKey& game, std::string_view name,
                                   std::int64_t createdAt, std::int64_t updatedAt) {
    PMM_DB_TRY_VALUE(stmt, c.prepare("INSERT INTO playsets (game_key, name, created_at, "
                                     "updated_at) VALUES (?, ?, ?, ?)"));
    PMM_TRY(stmt->bindAll(game, name, createdAt, updatedAt).run());
    return c.lastInsertRowId();
}

Status touch(Connection& c, std::int64_t id, std::int64_t now) {
    PMM_DB_TRY_VALUE(stmt, c.prepare("UPDATE playsets SET updated_at = ? WHERE id = ?"));
    return stmt->bindAll(now, id).run();
}

std::string snapshotToJson(const Playset& playset) {
    json entries = json::array();
    for (const auto& e : playset.entries) {
        entries.push_back({{"source", e.identity.source},
                           {"id", e.identity.id},
                           {"enabled", e.enabled},
                           {"nameHint", e.nameHint},
                           {"versionHint", e.versionHint},
                           {"fingerprint", e.fingerprint}});
    }
    const json snapshot = {{"format", kSnapshotFormat},
                           {"name", playset.name},
                           {"createdAt", playset.createdAt},
                           {"updatedAt", playset.updatedAt},
                           {"entries", std::move(entries)}};
    return snapshot.dump(-1, ' ', false, json::error_handler_t::replace);
}

Result<Playset> snapshotFromJson(std::string_view text, std::int64_t playsetId, const GameKey& game,
                                 std::int64_t historyId) {
    const std::string what = "playset history entry " + std::to_string(historyId);
    try {
        const json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded() || !j.is_object())
            return corruptValue(what, "snapshot is not a JSON object");
        if (j.value("format", 0) != kSnapshotFormat)
            return corruptValue(what, "unknown snapshot format");
        Playset out;
        out.id = playsetId;
        out.game = game;
        out.name = j.at("name").get<std::string>();
        out.createdAt = j.at("createdAt").get<std::int64_t>();
        out.updatedAt = j.at("updatedAt").get<std::int64_t>();
        for (const auto& e : j.at("entries")) {
            PlaysetEntry entry;
            entry.identity =
                ModIdentity{game, e.at("source").get<std::string>(), e.at("id").get<std::string>()};
            entry.enabled = e.at("enabled").get<bool>();
            entry.nameHint = e.at("nameHint").get<std::string>();
            entry.versionHint = e.at("versionHint").get<std::string>();
            entry.fingerprint = e.at("fingerprint").get<std::string>();
            out.entries.push_back(std::move(entry));
        }
        return out;
    } catch (const json::exception& ex) {
        return corruptValue(what, ex.what());
    }
}

Result<std::int64_t> insertHistory(Connection& c, const Playset& snapshot, std::string_view reason,
                                   std::int64_t now) {
    PMM_DB_TRY_VALUE(stmt, c.prepare("INSERT INTO playset_history (playset_id, game_key, reason, "
                                     "snapshot_json, created_at) VALUES (?, ?, ?, ?, ?)"));
    PMM_TRY(stmt->bindAll(snapshot.id, snapshot.game, reason, snapshotToJson(snapshot), now).run());
    return c.lastInsertRowId();
}

constexpr const char* kHistorySelect = "SELECT id, playset_id, game_key, reason, snapshot_json, "
                                       "created_at FROM playset_history";

Result<PlaysetHistoryEntry> readHistory(const Statement& stmt) {
    PlaysetHistoryEntry out;
    out.id = stmt.columnInt64(0);
    out.playsetId = stmt.columnInt64(1);
    out.game = stmt.columnText(2);
    out.reason = stmt.columnText(3);
    out.createdAt = stmt.columnInt64(5);
    PMM_DB_TRY_VALUE(snapshot,
                     snapshotFromJson(stmt.columnText(4), out.playsetId, out.game, out.id));
    out.snapshot = std::move(*snapshot);
    return out;
}

Result<std::vector<PlaysetHistoryEntry>> queryHistory(Statement& stmt) {
    std::vector<PlaysetHistoryEntry> out;
    for (;;) {
        PMM_DB_TRY_VALUE(row, stmt.step());
        if (!*row)
            break;
        PMM_DB_TRY_VALUE(entry, readHistory(stmt));
        out.push_back(std::move(*entry));
    }
    return out;
}

Result<PlaysetHistoryEntry> loadHistory(Connection& c, std::int64_t historyId) {
    PMM_DB_TRY_VALUE(stmt, c.prepare(std::string(kHistorySelect) + " WHERE id = ?"));
    stmt->bind(1, historyId);
    PMM_DB_TRY_VALUE(row, stmt->step());
    if (!*row)
        return makeError(ErrorCode::NotFound,
                         "This playset history entry no longer exists; it may have been purged.",
                         "history id " + std::to_string(historyId));
    return readHistory(*stmt);
}

} // namespace

Result<std::int64_t> PlaysetRepository::create(const Playset& playset) {
    if (playset.game.empty())
        return makeError(ErrorCode::InvalidArgument, "Cannot create a playset without a game.");
    PMM_TRY(validateName(playset.name));
    PMM_TRY(validateEntries(playset.game, playset.entries));
    auto guard = m_db.access();
    Connection& c = guard.connection();
    std::int64_t id = 0;
    PMM_TRY(m_db.transaction([&]() -> Status {
        const std::int64_t now = m_db.now();
        PMM_DB_TRY_VALUE(newId, insertPlayset(c, playset.game, playset.name, now, now));
        PMM_TRY(writeEntries(c, *newId, playset.entries));
        id = *newId;
        return {};
    }));
    return id;
}

Result<Playset> PlaysetRepository::get(std::int64_t id) {
    auto guard = m_db.access();
    Connection& c = guard.connection();
    Playset out;
    PMM_TRY(m_db.transaction(
        [&]() -> Status {
            PMM_DB_TRY_VALUE(playset, loadLivePlayset(c, id));
            out = std::move(*playset);
            return {};
        },
        TransactionMode::Deferred));
    return out;
}

Result<std::vector<PlaysetSummary>> PlaysetRepository::listForGame(const GameKey& game) {
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(stmt,
                     guard.connection().prepare(
                         "SELECT p.id, p.game_key, p.name, p.updated_at, "
                         "(SELECT count(*) FROM playset_entries e WHERE e.playset_id = p.id), "
                         "(SELECT count(*) FROM playset_entries e WHERE e.playset_id = p.id "
                         "AND e.enabled = 1) "
                         "FROM playsets p WHERE p.game_key = ? AND p.deleted_at IS NULL "
                         "ORDER BY p.name COLLATE NOCASE, p.id"));
    stmt->bind(1, game);
    std::vector<PlaysetSummary> out;
    for (;;) {
        PMM_DB_TRY_VALUE(row, stmt->step());
        if (!*row)
            break;
        PlaysetSummary summary;
        summary.id = stmt->columnInt64(0);
        summary.game = stmt->columnText(1);
        summary.name = stmt->columnText(2);
        summary.updatedAt = stmt->columnInt64(3);
        summary.entryCount = stmt->columnInt(4);
        summary.enabledCount = stmt->columnInt(5);
        out.push_back(std::move(summary));
    }
    return out;
}

Status PlaysetRepository::saveEntries(std::int64_t id, const std::vector<PlaysetEntry>& entries) {
    auto guard = m_db.access();
    Connection& c = guard.connection();
    return m_db.transaction([&]() -> Status {
        PMM_DB_TRY_VALUE(row, loadLiveRow(c, id));
        PMM_TRY(validateEntries(row->game, entries));
        PMM_TRY(writeEntries(c, id, entries));
        return touch(c, id, m_db.now());
    });
}

Status PlaysetRepository::rename(std::int64_t id, std::string_view name) {
    PMM_TRY(validateName(name));
    auto guard = m_db.access();
    Connection& c = guard.connection();
    PMM_DB_TRY_VALUE(stmt, c.prepare("UPDATE playsets SET name = ?, updated_at = ? "
                                     "WHERE id = ? AND deleted_at IS NULL"));
    PMM_TRY(stmt->bindAll(name, m_db.now(), id).run());
    if (c.changes() == 0)
        return playsetNotFound(id);
    return {};
}

Result<std::int64_t> PlaysetRepository::duplicate(std::int64_t id, std::string_view newName) {
    PMM_TRY(validateName(newName));
    auto guard = m_db.access();
    Connection& c = guard.connection();
    std::int64_t copyId = 0;
    PMM_TRY(m_db.transaction([&]() -> Status {
        PMM_DB_TRY_VALUE(original, loadLivePlayset(c, id));
        const std::int64_t now = m_db.now();
        PMM_DB_TRY_VALUE(newId, insertPlayset(c, original->game, newName, now, now));
        PMM_TRY(writeEntries(c, *newId, original->entries));
        copyId = *newId;
        return {};
    }));
    return copyId;
}

Result<std::int64_t> PlaysetRepository::remove(std::int64_t id) {
    auto guard = m_db.access();
    Connection& c = guard.connection();
    std::int64_t historyId = 0;
    PMM_TRY(m_db.transaction([&]() -> Status {
        PMM_DB_TRY_VALUE(current, loadLivePlayset(c, id));
        const std::int64_t now = m_db.now();
        PMM_DB_TRY_VALUE(newHistoryId, insertHistory(c, *current, history_reason::Deleted, now));
        PMM_DB_TRY_VALUE(
            stmt, c.prepare("UPDATE playsets SET deleted_at = ?, updated_at = ? WHERE id = ?"));
        PMM_TRY(stmt->bindAll(now, now, id).run());
        historyId = *newHistoryId;
        return {};
    }));
    return historyId;
}

Result<std::int64_t>
PlaysetRepository::replaceWithSnapshot(std::int64_t id, const std::vector<PlaysetEntry>& newEntries,
                                       std::string_view reason) {
    if (reason.empty())
        return makeError(ErrorCode::InvalidArgument, "A history reason is required.");
    auto guard = m_db.access();
    Connection& c = guard.connection();
    std::int64_t historyId = 0;
    PMM_TRY(m_db.transaction([&]() -> Status {
        PMM_DB_TRY_VALUE(current, loadLivePlayset(c, id));
        PMM_TRY(validateEntries(current->game, newEntries));
        const std::int64_t now = m_db.now();
        PMM_DB_TRY_VALUE(newHistoryId, insertHistory(c, *current, reason, now));
        PMM_TRY(writeEntries(c, id, newEntries));
        PMM_TRY(touch(c, id, now));
        historyId = *newHistoryId;
        return {};
    }));
    return historyId;
}

Result<std::vector<PlaysetHistoryEntry>> PlaysetRepository::historyForGame(const GameKey& game) {
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(stmt, guard.connection().prepare(std::string(kHistorySelect) +
                                                      " WHERE game_key = ? "
                                                      "ORDER BY created_at DESC, id DESC"));
    stmt->bind(1, game);
    return queryHistory(*stmt);
}

Result<std::vector<PlaysetHistoryEntry>>
PlaysetRepository::historyForPlayset(std::int64_t playsetId) {
    auto guard = m_db.access();
    PMM_DB_TRY_VALUE(stmt, guard.connection().prepare(std::string(kHistorySelect) +
                                                      " WHERE playset_id = ? "
                                                      "ORDER BY created_at DESC, id DESC"));
    stmt->bind(1, playsetId);
    return queryHistory(*stmt);
}

Result<PlaysetHistoryEntry> PlaysetRepository::historyEntry(std::int64_t historyId) {
    auto guard = m_db.access();
    return loadHistory(guard.connection(), historyId);
}

Result<std::int64_t> PlaysetRepository::restore(std::int64_t historyId) {
    auto guard = m_db.access();
    Connection& c = guard.connection();
    std::int64_t restoredId = 0;
    PMM_TRY(m_db.transaction([&]() -> Status {
        PMM_DB_TRY_VALUE(entry, loadHistory(c, historyId));
        const Playset& snapshot = entry->snapshot;
        const std::int64_t now = m_db.now();
        PMM_DB_TRY_VALUE(row, loadRow(c, entry->playsetId));
        if (row->has_value() && (*row)->game == entry->game) {
            const PlaysetRow& existing = **row;
            if (!existing.deletedAt) {
                PMM_DB_TRY_VALUE(current, loadLivePlayset(c, existing.id));
                PMM_DB_TRY_VALUE(undoId,
                                 insertHistory(c, *current, history_reason::BeforeRestore, now));
            }
            PMM_DB_TRY_VALUE(stmt, c.prepare("UPDATE playsets SET name = ?, deleted_at = NULL, "
                                             "updated_at = ? WHERE id = ?"));
            PMM_TRY(stmt->bindAll(snapshot.name, now, existing.id).run());
            PMM_TRY(writeEntries(c, existing.id, snapshot.entries));
            restoredId = existing.id;
            return {};
        }
        // The playset was purged: recreate it under a new id.
        const std::int64_t createdAt = snapshot.createdAt > 0 ? snapshot.createdAt : now;
        PMM_DB_TRY_VALUE(newId, insertPlayset(c, entry->game, snapshot.name, createdAt, now));
        PMM_TRY(writeEntries(c, *newId, snapshot.entries));
        restoredId = *newId;
        return {};
    }));
    return restoredId;
}

Result<int> PlaysetRepository::purgeHistoryOlderThan(std::int64_t cutoffUnixMs, int keepAtLeast) {
    if (keepAtLeast < 0)
        return makeError(ErrorCode::InvalidArgument, "keepAtLeast must not be negative.");
    auto guard = m_db.access();
    Connection& c = guard.connection();
    int purged = 0;
    PMM_TRY(m_db.transaction([&]() -> Status {
        PMM_DB_TRY_VALUE(history,
                         c.prepare("DELETE FROM playset_history WHERE id IN ("
                                   "SELECT id FROM (SELECT id, created_at, ROW_NUMBER() OVER ("
                                   "PARTITION BY playset_id ORDER BY created_at DESC, id DESC) "
                                   "AS rn FROM playset_history) "
                                   "WHERE rn > ? AND created_at < ?)"));
        PMM_TRY(history->bindAll(keepAtLeast, cutoffUnixMs).run());
        purged = c.changes();
        PMM_TRY(c.exec("DELETE FROM playsets WHERE deleted_at IS NOT NULL AND NOT EXISTS ("
                       "SELECT 1 FROM playset_history h WHERE h.playset_id = playsets.id)"));
        return {};
    }));
    return purged;
}

} // namespace pmm::persistence
