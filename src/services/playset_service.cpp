#include "services/playset_service.hpp"

#include "core/catalog.hpp"
#include "core/import_plan.hpp"
#include "persistence/inventory_repository.hpp"
#include "persistence/settings_repository.hpp"
#include "services/internal/util.hpp"
#include "sharing/manifest.hpp"
#include "sharing/utf8.hpp"

#include <algorithm>
#include <functional>
#include <string_view>
#include <tuple>
#include <utility>

namespace pmm::services {
namespace {

// Identity games are ignored: entries are always scoped to the playset's game
// and the repository reads an empty game back as the playset's.
std::size_t entriesHash(const std::vector<PlaysetEntry>& entries) {
    std::size_t hash = entries.size();
    const auto mix = [&hash](std::string_view text) {
        hash ^= std::hash<std::string_view>{}(text) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    };
    for (const PlaysetEntry& e : entries) {
        mix(e.identity.source);
        mix(e.identity.id);
        mix(e.enabled ? "1" : "0");
        mix(e.nameHint);
        mix(e.versionHint);
        mix(e.fingerprint);
    }
    return hash;
}

char asciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoringAsciiCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return asciiLower(x) == asciiLower(y);
           });
}

Status checkReorder(const ViewFilter& filter) {
    const ReorderPolicy policy = reorderPolicy(filter);
    if (!policy.allowed)
        return makeError(ErrorCode::ValidationFailed, policy.explanation);
    return {};
}

Result<InstalledModMap> inventoryMap(persistence::Database& db, const GameKey& game) {
    persistence::InventoryRepository inventory(db);
    auto mods = inventory.list(game);
    if (!mods)
        return std::move(mods).error();
    return detail::toMap(mods.value());
}

// Issues present in `after` but not in `before` (same entry, name and kind).
std::vector<DependencyIssue> newIssues(const std::vector<DependencyIssue>& before,
                                       const std::vector<DependencyIssue>& after) {
    std::vector<DependencyIssue> out;
    for (const DependencyIssue& issue : after) {
        const bool existed =
            std::any_of(before.begin(), before.end(), [&](const DependencyIssue& b) {
                return std::tie(b.entryIndex, b.dependencyName, b.kind) ==
                       std::tie(issue.entryIndex, issue.dependencyName, issue.kind);
            });
        if (!existed)
            out.push_back(issue);
    }
    return out;
}

} // namespace

Result<Playset> PlaysetService::load(std::int64_t playsetId) {
    persistence::PlaysetRepository repo(m_ctx.database);
    auto playset = repo.get(playsetId);
    if (!playset)
        return std::move(playset).error();
    for (PlaysetEntry& entry : playset.value().entries)
        entry.identity.game = playset.value().game;
    return playset;
}

Result<std::string> PlaysetService::checkedName(const std::string& name) const {
    std::string out = detail::trimmed(name);
    if (!sharing::isValidUtf8(out) || sharing::containsControlCharacters(out))
        return makeError(ErrorCode::InvalidArgument,
                         "A playset name must be plain text without control characters.");
    if (out.size() > sharing::kMaxPlaysetNameBytes)
        return makeError(ErrorCode::InvalidArgument,
                         "A playset name can be at most " +
                             std::to_string(sharing::kMaxPlaysetNameBytes) +
                             " bytes long (so that it fits into share codes).");
    return out;
}

Result<std::vector<PlaysetSummary>> PlaysetService::listPlaysets(const GameKey& game) {
    persistence::PlaysetRepository repo(m_ctx.database);
    return repo.listForGame(game);
}

Result<Playset> PlaysetService::playset(std::int64_t playsetId) {
    return load(playsetId);
}

Result<std::optional<std::int64_t>> PlaysetService::activePlaysetId(const GameKey& game) {
    persistence::SettingsRepository settings(m_ctx.database);
    persistence::PlaysetRepository repo(m_ctx.database);
    auto stored = settings.getInt(settings_key::activePlayset(game));
    if (stored && stored.value()) {
        auto playset = repo.get(*stored.value());
        if (playset && playset.value().game == game)
            return std::optional<std::int64_t>(playset.value().id);
        if (!playset && playset.error().code != ErrorCode::NotFound)
            return std::move(playset).error();
    } else if (!stored && stored.error().code != ErrorCode::ParseError) {
        return std::move(stored).error();
    }
    auto all = repo.listForGame(game);
    if (!all)
        return std::move(all).error();
    if (all.value().empty())
        return std::optional<std::int64_t>();
    return std::optional<std::int64_t>(all.value().front().id);
}

Status PlaysetService::setActivePlayset(const GameKey& game, std::int64_t playsetId) {
    persistence::PlaysetRepository repo(m_ctx.database);
    auto playset = repo.get(playsetId);
    if (!playset)
        return std::move(playset).error();
    if (playset.value().game != game)
        return makeError(ErrorCode::InvalidArgument, "That playset belongs to " +
                                                         gameDisplayName(playset.value().game) +
                                                         ", not " + gameDisplayName(game) + ".");
    return m_ctx.database.transaction([&]() -> Status {
        persistence::SettingsRepository settings(m_ctx.database);
        PMM_TRY(settings.setInt(settings_key::activePlayset(game), playsetId));
        return settings.set(settings_key::LastGame, game);
    });
}

Result<std::optional<GameKey>> PlaysetService::lastGameKey() {
    persistence::SettingsRepository settings(m_ctx.database);
    auto stored = settings.get(settings_key::LastGame);
    if (!stored)
        return std::move(stored).error();
    if (!stored.value() || !findGame(*stored.value()))
        return std::optional<GameKey>();
    return std::optional<GameKey>(*stored.value());
}

Status PlaysetService::setLastGameKey(const GameKey& game) {
    if (!findGame(game))
        return makeError(ErrorCode::NotFound, "\"" + game + "\" is not a known game.");
    persistence::SettingsRepository settings(m_ctx.database);
    return settings.set(settings_key::LastGame, game);
}

Result<std::int64_t> PlaysetService::create(const GameKey& game, const std::string& name) {
    if (!findGame(game))
        return makeError(ErrorCode::NotFound, "\"" + game + "\" is not a known game.");
    auto checked = checkedName(name);
    if (!checked)
        return std::move(checked).error();
    auto existing = listPlaysets(game);
    if (!existing)
        return std::move(existing).error();
    Playset playset;
    playset.game = game;
    playset.name = uniquePlaysetName(checked.value().empty() ? kDefaultName : checked.value(),
                                     detail::names(existing.value()));
    persistence::PlaysetRepository repo(m_ctx.database);
    return repo.create(playset);
}

Result<std::int64_t> PlaysetService::duplicate(std::int64_t playsetId, const std::string& name) {
    auto source = load(playsetId);
    if (!source)
        return std::move(source).error();
    auto checked = checkedName(name);
    if (!checked)
        return std::move(checked).error();
    std::string wanted =
        checked.value().empty() ? source.value().name + " (copy)" : checked.value();
    auto existing = listPlaysets(source.value().game);
    if (!existing)
        return std::move(existing).error();
    wanted = uniquePlaysetName(wanted, detail::names(existing.value()));
    if (wanted.size() > sharing::kMaxPlaysetNameBytes)
        wanted = uniquePlaysetName(source.value().name, detail::names(existing.value()));
    persistence::PlaysetRepository repo(m_ctx.database);
    return repo.duplicate(playsetId, wanted);
}

Status PlaysetService::rename(std::int64_t playsetId, const std::string& name) {
    auto checked = checkedName(name);
    if (!checked)
        return std::move(checked).error();
    if (checked.value().empty())
        return makeError(ErrorCode::InvalidArgument, "A playset name must not be empty.");
    auto playset = load(playsetId);
    if (!playset)
        return std::move(playset).error();
    auto existing = listPlaysets(playset.value().game);
    if (!existing)
        return std::move(existing).error();
    for (const PlaysetSummary& other : existing.value()) {
        if (other.id != playsetId && equalsIgnoringAsciiCase(other.name, checked.value()))
            return makeError(ErrorCode::AlreadyExists, "Another playset is already called \"" +
                                                           other.name +
                                                           "\". Choose a different name.");
    }
    persistence::PlaysetRepository repo(m_ctx.database);
    return repo.rename(playsetId, checked.value());
}

Result<std::int64_t> PlaysetService::remove(std::int64_t playsetId) {
    auto playset = load(playsetId);
    if (!playset)
        return std::move(playset).error();
    const GameKey game = playset.value().game;
    persistence::SettingsRepository settings(m_ctx.database);
    auto active = settings.getInt(settings_key::activePlayset(game));
    const bool wasActive = active && active.value() && *active.value() == playsetId;

    persistence::PlaysetRepository repo(m_ctx.database);
    auto historyId = repo.remove(playsetId);
    if (!historyId)
        return std::move(historyId).error();
    m_stacks.erase(playsetId);
    if (wasActive) {
        auto remaining = repo.listForGame(game);
        if (remaining && !remaining.value().empty())
            PMM_TRY(
                settings.setInt(settings_key::activePlayset(game), remaining.value().front().id));
        else
            PMM_TRY(settings.remove(settings_key::activePlayset(game)));
    }
    return historyId;
}

Result<std::optional<persistence::PlaysetHistoryEntry>>
PlaysetService::lastDeleted(const GameKey& game) {
    persistence::PlaysetRepository repo(m_ctx.database);
    auto history = repo.historyForGame(game);
    if (!history)
        return std::move(history).error();
    for (persistence::PlaysetHistoryEntry& entry : history.value()) {
        if (entry.reason != persistence::history_reason::Deleted)
            continue;
        auto live = repo.get(entry.playsetId);
        if (live)
            continue; // restored since
        if (live.error().code != ErrorCode::NotFound)
            return std::move(live).error();
        return std::optional<persistence::PlaysetHistoryEntry>(std::move(entry));
    }
    return std::optional<persistence::PlaysetHistoryEntry>();
}

Result<std::int64_t> PlaysetService::restoreLastDeleted(const GameKey& game) {
    auto last = lastDeleted(game);
    if (!last)
        return std::move(last).error();
    if (!last.value())
        return makeError(ErrorCode::NotFound, "There is no deleted playset to restore.");
    return restoreHistory(last.value()->id);
}

Result<std::vector<persistence::PlaysetHistoryEntry>> PlaysetService::history(const GameKey& game) {
    persistence::PlaysetRepository repo(m_ctx.database);
    return repo.historyForGame(game);
}

Result<std::int64_t> PlaysetService::restoreHistory(std::int64_t historyId) {
    persistence::PlaysetRepository repo(m_ctx.database);
    auto entry = repo.historyEntry(historyId);
    if (!entry)
        return std::move(entry).error();
    auto restored = repo.restore(historyId);
    if (!restored)
        return std::move(restored).error();
    m_stacks.erase(restored.value());
    PMM_TRY(setActivePlayset(entry.value().game, restored.value()));
    return restored;
}

// --- Editing -------------------------------------------------------------------

Status PlaysetService::commit(std::int64_t playsetId, std::string label,
                              const std::vector<PlaysetEntry>& before,
                              const std::vector<PlaysetEntry>& after, const char* historyReason) {
    if (before == after)
        return {};
    persistence::PlaysetRepository repo(m_ctx.database);
    if (historyReason) {
        auto snapshot = repo.replaceWithSnapshot(playsetId, after, historyReason);
        if (!snapshot)
            return std::move(snapshot).error();
    } else {
        PMM_TRY(repo.saveEntries(playsetId, after));
    }
    Stacks& stacks = m_stacks[playsetId];
    stacks.undo.push_back(Step{std::move(label), before, entriesHash(after)});
    while (stacks.undo.size() > kUndoLimit)
        stacks.undo.pop_front();
    stacks.redo.clear();
    return {};
}

Result<std::vector<DependencyIssue>>
PlaysetService::setEnabled(std::int64_t playsetId, std::span<const std::size_t> indices,
                           bool enabled) {
    auto playset = load(playsetId);
    if (!playset)
        return std::move(playset).error();
    const std::vector<PlaysetEntry>& before = playset.value().entries;
    std::vector<PlaysetEntry> after = before;
    auto changed = pmm::setEnabled(after, indices, enabled);
    if (!changed)
        return std::move(changed).error();
    auto inventory = inventoryMap(m_ctx.database, playset.value().game);
    if (!inventory)
        return std::move(inventory).error();

    std::vector<DependencyIssue> issues =
        enabled ? dependencyIssuesIntroducedByEnabling(before, inventory.value(), indices)
                : newIssues(checkDependencies(before, inventory.value()),
                            checkDependencies(after, inventory.value()));
    if (changed.value() == 0)
        return issues;
    const std::string label =
        std::string(enabled ? "Enable " : "Disable ") + detail::countLabel(before, indices);
    PMM_TRY(commit(playsetId, label, before, after));
    return issues;
}

Status PlaysetService::move(std::int64_t playsetId, std::size_t from, std::size_t to,
                            const ViewFilter& filter) {
    PMM_TRY(checkReorder(filter));
    auto playset = load(playsetId);
    if (!playset)
        return std::move(playset).error();
    std::vector<PlaysetEntry> after = playset.value().entries;
    PMM_TRY(moveEntry(after, from, to));
    const std::size_t index[] = {from};
    return commit(playsetId, "Move " + detail::countLabel(playset.value().entries, index),
                  playset.value().entries, after);
}

Result<std::vector<std::size_t>> PlaysetService::moveUp(std::int64_t playsetId,
                                                        std::span<const std::size_t> indices,
                                                        const ViewFilter& filter) {
    PMM_TRY(checkReorder(filter));
    auto playset = load(playsetId);
    if (!playset)
        return std::move(playset).error();
    std::vector<PlaysetEntry> after = playset.value().entries;
    auto moved = moveSelectionUp(after, indices);
    if (!moved)
        return std::move(moved).error();
    PMM_TRY(commit(playsetId,
                   "Move " + detail::countLabel(playset.value().entries, indices) + " up",
                   playset.value().entries, after));
    return moved;
}

Result<std::vector<std::size_t>> PlaysetService::moveDown(std::int64_t playsetId,
                                                          std::span<const std::size_t> indices,
                                                          const ViewFilter& filter) {
    PMM_TRY(checkReorder(filter));
    auto playset = load(playsetId);
    if (!playset)
        return std::move(playset).error();
    std::vector<PlaysetEntry> after = playset.value().entries;
    auto moved = moveSelectionDown(after, indices);
    if (!moved)
        return std::move(moved).error();
    PMM_TRY(commit(playsetId,
                   "Move " + detail::countLabel(playset.value().entries, indices) + " down",
                   playset.value().entries, after));
    return moved;
}

Result<std::size_t> PlaysetService::moveBlock(std::int64_t playsetId,
                                              std::span<const std::size_t> indices,
                                              std::size_t insertBefore, const ViewFilter& filter) {
    PMM_TRY(checkReorder(filter));
    auto playset = load(playsetId);
    if (!playset)
        return std::move(playset).error();
    std::vector<PlaysetEntry> after = playset.value().entries;
    auto first = pmm::moveBlock(after, indices, insertBefore);
    if (!first)
        return std::move(first).error();
    PMM_TRY(commit(playsetId, "Move " + detail::countLabel(playset.value().entries, indices),
                   playset.value().entries, after));
    return first;
}

Status PlaysetService::addMods(std::int64_t playsetId, std::span<const ModIdentity> identities) {
    auto playset = load(playsetId);
    if (!playset)
        return std::move(playset).error();
    const GameKey& game = playset.value().game;
    persistence::InventoryRepository inventory(m_ctx.database);
    std::vector<PlaysetEntry> added;
    for (ModIdentity identity : identities) {
        if (identity.game.empty())
            identity.game = game;
        if (identity.game != game)
            return makeError(ErrorCode::InvalidArgument,
                             "Only mods of " + gameDisplayName(game) +
                                 " can be added to this playset.",
                             toString(identity));
        auto found = inventory.find(identity);
        if (!found)
            return std::move(found).error();
        if (!found.value())
            return makeError(ErrorCode::NotFound,
                             "The mod " + identity.id + " is not installed, so it cannot be added.",
                             toString(identity));
        PlaysetEntry entry;
        entry.identity = identity;
        entry.enabled = true;
        entry.nameHint = found.value()->descriptor.name;
        added.push_back(std::move(entry));
    }
    if (added.empty())
        return {};
    std::vector<PlaysetEntry> after = playset.value().entries;
    PMM_TRY(addEntries(after, added));
    const std::string label = added.size() == 1
                                  ? "Add \"" + detail::entryLabel(added.front()) + "\""
                                  : "Add " + std::to_string(added.size()) + " mods";
    return commit(playsetId, label, playset.value().entries, after);
}

Status PlaysetService::removeEntries(std::int64_t playsetId, std::span<const std::size_t> indices) {
    auto playset = load(playsetId);
    if (!playset)
        return std::move(playset).error();
    std::vector<PlaysetEntry> after = playset.value().entries;
    auto removed = pmm::removeEntries(after, indices);
    if (!removed)
        return std::move(removed).error();
    return commit(playsetId, "Remove " + detail::countLabel(playset.value().entries, indices),
                  playset.value().entries, after, kHistoryReasonEntriesRemoved);
}

Status PlaysetService::undo(std::int64_t playsetId) {
    const auto it = m_stacks.find(playsetId);
    if (it == m_stacks.end() || it->second.undo.empty())
        return makeError(ErrorCode::InvalidArgument, "There is nothing to undo.");
    auto playset = load(playsetId);
    if (!playset) {
        if (playset.error().code == ErrorCode::NotFound)
            m_stacks.erase(it);
        return std::move(playset).error();
    }
    const Step& step = it->second.undo.back();
    if (entriesHash(playset.value().entries) != step.expectedHash) {
        m_stacks.erase(it);
        return makeError(ErrorCode::ValidationFailed,
                         "The playset was changed elsewhere (for example by an import or a "
                         "restore), so its undo history no longer applies and was cleared.");
    }
    persistence::PlaysetRepository repo(m_ctx.database);
    PMM_TRY(repo.saveEntries(playsetId, step.entries));
    it->second.redo.push_back(Step{step.label, playset.value().entries, entriesHash(step.entries)});
    it->second.undo.pop_back();
    return {};
}

Status PlaysetService::redo(std::int64_t playsetId) {
    const auto it = m_stacks.find(playsetId);
    if (it == m_stacks.end() || it->second.redo.empty())
        return makeError(ErrorCode::InvalidArgument, "There is nothing to redo.");
    auto playset = load(playsetId);
    if (!playset) {
        if (playset.error().code == ErrorCode::NotFound)
            m_stacks.erase(it);
        return std::move(playset).error();
    }
    const Step& step = it->second.redo.back();
    if (entriesHash(playset.value().entries) != step.expectedHash) {
        m_stacks.erase(it);
        return makeError(ErrorCode::ValidationFailed,
                         "The playset was changed elsewhere (for example by an import or a "
                         "restore), so its undo history no longer applies and was cleared.");
    }
    persistence::PlaysetRepository repo(m_ctx.database);
    PMM_TRY(repo.saveEntries(playsetId, step.entries));
    it->second.undo.push_back(Step{step.label, playset.value().entries, entriesHash(step.entries)});
    it->second.redo.pop_back();
    return {};
}

UndoInfo PlaysetService::undoInfo(std::int64_t playsetId) const {
    UndoInfo info;
    const auto it = m_stacks.find(playsetId);
    if (it == m_stacks.end())
        return info;
    if (!it->second.undo.empty()) {
        info.canUndo = true;
        info.undoText = it->second.undo.back().label;
    }
    if (!it->second.redo.empty()) {
        info.canRedo = true;
        info.redoText = it->second.redo.back().label;
    }
    return info;
}

void PlaysetService::clearUndoHistory(std::int64_t playsetId) {
    m_stacks.erase(playsetId);
}

} // namespace pmm::services
