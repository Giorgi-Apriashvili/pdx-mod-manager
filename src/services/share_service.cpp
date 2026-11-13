#include "services/share_service.hpp"

#include "core/catalog.hpp"
#include "core/log.hpp"
#include "core/utf8.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "persistence/installation_repository.hpp"
#include "persistence/inventory_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "persistence/settings_repository.hpp"
#include "services/internal/util.hpp"
#include "sharing/share_code.hpp"
#include "sharing/utf8.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <utility>

namespace pmm::services {
namespace {

constexpr const char* kComponent = "services.share";
constexpr const char* kFallbackPlaysetName = "Shared playset";

// Display text fitted to the manifest rules: well-formed UTF-8 (otherwise
// dropped), control characters replaced by spaces, trimmed, and cut on a
// character boundary to at most `maxBytes`.
std::string fitHint(std::string_view text, std::size_t maxBytes) {
    if (!isValidUtf8(text))
        return {};
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t length = utf8SequenceLength(text, i); // 1..4: `text` is well-formed
        const bool c0 = lead < 0x20 || lead == 0x7F;
        const bool c1 =
            length == 2 && lead == 0xC2 && static_cast<unsigned char>(text[i + 1]) < 0xA0;
        if (c0 || c1)
            out += ' ';
        else
            out.append(text.substr(i, length));
        i += length;
    }
    out = detail::trimmed(out);
    if (out.size() > maxBytes)
        out = detail::trimmed(utf8Prefix(out, maxBytes));
    return out;
}

bool isFingerprint(std::string_view text) {
    constexpr std::string_view kPrefix = "sha256:";
    if (text.size() != kPrefix.size() + 64 || text.substr(0, kPrefix.size()) != kPrefix)
        return false;
    return std::all_of(text.begin() + static_cast<std::ptrdiff_t>(kPrefix.size()), text.end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

char asciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoringAsciiCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return asciiLower(x) == asciiLower(y);
           });
}

Status checkPlaysetName(const std::string& name) {
    if (name.empty())
        return makeError(ErrorCode::InvalidArgument, "A playset name must not be empty.");
    if (!sharing::isValidUtf8(name) || sharing::containsControlCharacters(name))
        return makeError(ErrorCode::InvalidArgument,
                         "A playset name must be plain text without control characters.");
    if (name.size() > sharing::kMaxPlaysetNameBytes)
        return makeError(ErrorCode::InvalidArgument,
                         "A playset name can be at most " +
                             std::to_string(sharing::kMaxPlaysetNameBytes) + " bytes long.");
    return {};
}

Error notSaved(const Error& error) {
    return Error(error.code, "The import was not saved and nothing was changed. " + error.message,
                 error.detail);
}

// "share code" or "Workshop ID list", for messages.
std::string textName(sharing::ImportTextFormat format) {
    return format == sharing::ImportTextFormat::ShareCode ? "share code" : "Workshop ID list";
}

Error cannotManage(sharing::ImportTextFormat format, const GameKey& game) {
    const std::string name = gameDisplayName(game);
    if (format == sharing::ImportTextFormat::ShareCode)
        return Error(ErrorCode::UnsupportedGame,
                     "This share code is for " + name +
                         ", which this build of Paradox Mod Manager cannot manage.");
    return Error(ErrorCode::UnsupportedGame,
                 "This build of Paradox Mod Manager cannot manage " + name +
                     ", so the Workshop ID list cannot be imported for it.");
}

std::string countText(std::size_t count, const char* singular, const char* plural) {
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

// The catalog game whose Steam app is `appId`, or "".
GameKey gameForAppId(std::string_view appId) {
    for (const GameCatalogEntry& entry : gameCatalog()) {
        if (!appId.empty() && entry.steamAppId == appId)
            return GameKey(entry.key);
    }
    return {};
}

WorkshopItemCheck toCheck(providers::WorkshopItemVerdict verdict) {
    switch (verdict) {
    case providers::WorkshopItemVerdict::Ok:
        return WorkshopItemCheck::Ok;
    case providers::WorkshopItemVerdict::NotFound:
        return WorkshopItemCheck::NotFound;
    case providers::WorkshopItemVerdict::NoDetails:
        return WorkshopItemCheck::Unavailable;
    case providers::WorkshopItemVerdict::Banned:
        return WorkshopItemCheck::Banned;
    case providers::WorkshopItemVerdict::WrongGame:
        return WorkshopItemCheck::WrongGame;
    }
    return WorkshopItemCheck::Unavailable;
}

// The warnings withWorkshopMetadata() adds for the answered missing entries.
std::vector<std::string> workshopWarnings(const ImportPreview& preview,
                                          std::span<const std::string> ids) {
    std::size_t notFound = 0;
    std::size_t unavailable = 0;
    std::size_t banned = 0;
    std::size_t wrongGame = 0;
    std::size_t found = 0;
    std::set<GameKey> otherGames;
    for (const std::string& id : ids) {
        const auto it = preview.workshop.find(id);
        if (it == preview.workshop.end())
            continue;
        switch (it->second.check) {
        case WorkshopItemCheck::Ok:
            ++found;
            break;
        case WorkshopItemCheck::NotFound:
            ++notFound;
            break;
        case WorkshopItemCheck::Unavailable:
            ++unavailable;
            break;
        case WorkshopItemCheck::Banned:
            ++found;
            ++banned;
            break;
        case WorkshopItemCheck::WrongGame:
            ++found;
            ++wrongGame;
            otherGames.insert(it->second.otherGame);
            break;
        }
    }

    std::vector<std::string> warnings;
    if (notFound > 0)
        warnings.push_back(
            countText(notFound, "missing Workshop mod was", "missing Workshop mods were") +
            " not found on the Steam Workshop. " + (notFound == 1 ? "It" : "They") +
            " may have been deleted, or " + (notFound == 1 ? "it is" : "they are") +
            " private or friends-only.");
    if (unavailable > 0)
        warnings.push_back("Steam gave no details for " +
                           countText(unavailable, "missing Workshop mod", "missing Workshop mods") +
                           ", so " + (unavailable == 1 ? "it" : "they") + " may not download.");
    if (banned > 0)
        warnings.push_back(
            countText(banned, "missing Workshop mod is", "missing Workshop mods are") +
            " banned on the Steam Workshop and cannot be downloaded.");
    if (wrongGame > 0) {
        const ImportCounts& counts = preview.plan->counts;
        const GameCatalogEntry* other =
            otherGames.size() == 1 ? findGame(*otherGames.begin()) : nullptr;
        // Every found item is that game's, and none is installed here (an
        // installed entry shows the list is, at least partly, this game's).
        if (preview.format == sharing::ImportTextFormat::WorkshopIdList && other &&
            wrongGame == found && counts.installed == 0 && counts.broken == 0) {
            const std::string name(other->displayName);
            warnings.push_back("These are " + name + " mods, not " + preview.gameName +
                               " mods: select " + name + " and paste the list again.");
        } else {
            warnings.push_back(
                countText(wrongGame, "missing Workshop mod is", "missing Workshop mods are") +
                " for another game, not " + preview.gameName + ".");
        }
    }
    return warnings;
}

} // namespace

std::vector<std::string> workshopIdsToLookUp(const ImportPreview& preview) {
    std::vector<std::string> ids;
    if (!preview.ok() || !preview.plan)
        return ids;
    std::set<std::string_view> seen;
    for (const ImportPlanEntry& entry : preview.plan->entries) {
        if (entry.status == ImportEntryStatus::Missing &&
            entry.entry.identity.source == source::SteamWorkshop &&
            seen.insert(entry.entry.identity.id).second)
            ids.push_back(entry.entry.identity.id);
    }
    return ids;
}

ImportPreview withWorkshopMetadata(ImportPreview preview,
                                   std::span<const providers::WorkshopItemMetadata> items) {
    if (!preview.ok() || !preview.manifest || !preview.plan)
        return preview;
    const std::vector<std::string> ids = workshopIdsToLookUp(preview);
    const std::set<std::string_view> missing(ids.begin(), ids.end());
    for (const providers::WorkshopItemMetadata& item : items) {
        if (missing.count(item.id) == 0)
            continue;
        PreviewWorkshopItem answer;
        answer.check = toCheck(providers::classifyWorkshopItemForGame(item, preview.steamAppId));
        if (item.found)
            answer.title = fitHint(item.title, sharing::kMaxNameHintBytes);
        if (answer.check != WorkshopItemCheck::Ok) {
            const Status valid = providers::validateWorkshopItemForGame(item, preview.steamAppId);
            if (!valid)
                answer.message = valid.error().message;
        }
        if (answer.check == WorkshopItemCheck::WrongGame)
            answer.otherGame = gameForAppId(item.consumerAppId);
        preview.workshop.insert_or_assign(item.id, std::move(answer));
    }

    // The title replaces the text's own hint, in what is shown and stored.
    sharing::Manifest& manifest = *preview.manifest;
    ImportPlan& plan = *preview.plan;
    for (std::size_t i = 0; i < manifest.mods.size() && i < plan.entries.size(); ++i) {
        sharing::ManifestEntry& mod = manifest.mods[i];
        ImportPlanEntry& entry = plan.entries[i];
        if (entry.status != ImportEntryStatus::Missing || mod.source != source::SteamWorkshop)
            continue;
        const auto it = preview.workshop.find(mod.id);
        if (it == preview.workshop.end() || it->second.title.empty())
            continue;
        mod.nameHint = it->second.title;
        entry.entry.nameHint = it->second.title;
    }

    // Before the standing note, replacing those of an earlier call.
    std::vector<std::string>& warnings = plan.warnings;
    auto note = std::find(warnings.begin(), warnings.end(),
                          std::string_view(kIdenticalContentNotGuaranteed));
    const auto earlier = static_cast<std::ptrdiff_t>(std::min<std::size_t>(
        preview.workshopWarnings, static_cast<std::size_t>(note - warnings.begin())));
    note = warnings.erase(note - earlier, note);
    std::vector<std::string> added = workshopWarnings(preview, ids);
    preview.workshopWarnings = added.size();
    warnings.insert(note, std::make_move_iterator(added.begin()),
                    std::make_move_iterator(added.end()));
    return preview;
}

Result<std::string> ShareService::exportCode(std::int64_t playsetId) {
    persistence::PlaysetRepository playsets(m_ctx.database);
    auto playset = playsets.get(playsetId);
    if (!playset)
        return std::move(playset).error();
    Playset exported = std::move(playset).value();
    const GameKey game = exported.game;

    persistence::InstallationRepository installations(m_ctx.database);
    auto installation = installations.get(game);
    if (!installation)
        return std::move(installation).error();
    persistence::InventoryRepository inventory(m_ctx.database);
    auto listed = inventory.list(game);
    if (!listed)
        return std::move(listed).error();
    std::vector<InstalledMod> mods = std::move(listed).value();
    std::map<ModIdentity, std::size_t> byIdentity;
    for (std::size_t i = 0; i < mods.size(); ++i)
        byIdentity.emplace(mods[i].identity, i);

    bool fingerprintsChanged = false;
    for (PlaysetEntry& entry : exported.entries) {
        entry.identity.game = game;
        const auto found = byIdentity.find(entry.identity);
        if (found != byIdentity.end()) {
            InstalledMod& mod = mods[found->second];
            if (std::string name = fitHint(mod.descriptor.name, sharing::kMaxNameHintBytes);
                !name.empty())
                entry.nameHint = std::move(name);
            if (std::string version =
                    fitHint(mod.descriptor.version, sharing::kMaxVersionHintBytes);
                !version.empty())
                entry.versionHint = std::move(version);
            if (entry.identity.source == source::Local && m_ctx.fingerprint &&
                !mod.contentPath.empty()) {
                // Always recomputed: a cached value cannot be trusted, because
                // editing a file inside the mod folder leaves the folder's
                // modification time unchanged.
                std::string current;
                auto fingerprint = m_ctx.fingerprint(mod.contentPath);
                if (fingerprint && isFingerprint(fingerprint.value())) {
                    current = std::move(fingerprint).value();
                } else {
                    const std::string why = fingerprint ? std::string("malformed fingerprint")
                                                        : fingerprint.error().message;
                    logWarning(kComponent, "local mod fingerprint unavailable",
                               {{"mod", toString(mod.identity)}, {"error", why}});
                }
                if (current != mod.fingerprint) {
                    mod.fingerprint = current;
                    fingerprintsChanged = true;
                }
                entry.fingerprint = std::move(current);
            }
        }
        entry.nameHint = fitHint(entry.nameHint, sharing::kMaxNameHintBytes);
        entry.versionHint = fitHint(entry.versionHint, sharing::kMaxVersionHintBytes);
        if (!isFingerprint(entry.fingerprint))
            entry.fingerprint.clear();
    }
    if (fingerprintsChanged) {
        // Cached for readiness comparisons until the next rescan; failing to
        // cache does not affect the code.
        if (Status cached = inventory.replaceForGame(game, mods); !cached)
            logWarning(kComponent, "caching fingerprints failed",
                       {{"error", cached.error().message}});
    }

    exported.name = fitHint(exported.name, sharing::kMaxPlaysetNameBytes);
    if (exported.name.empty())
        exported.name = kFallbackPlaysetName;
    const std::string versionHint =
        installation.value() ? fitHint(installation.value()->version, sharing::kMaxVersionHintBytes)
                             : std::string();
    auto manifest = sharing::manifestFromPlayset(exported, versionHint);
    if (!manifest)
        return std::move(manifest).error();
    return sharing::encodeShareCode(manifest.value());
}

Result<sharing::WorkshopIdListExport> ShareService::exportWorkshopIdList(std::int64_t playsetId) {
    persistence::PlaysetRepository playsets(m_ctx.database);
    auto playset = playsets.get(playsetId);
    if (!playset)
        return std::move(playset).error();
    Playset exported = std::move(playset).value();
    for (PlaysetEntry& entry : exported.entries)
        entry.identity.game = exported.game; // as exportCode() shares them
    return sharing::workshopIdListFromPlayset(exported);
}

ImportPreview ShareService::previewImport(std::string_view text, const GameKey& idListGame) {
    const sharing::ImportTextFormat format = sharing::detectImportTextFormat(text);
    ImportPreview failed;
    failed.format = format;
    if (format == sharing::ImportTextFormat::ShareCode) {
        auto manifest = sharing::decodeShareCode(text);
        if (!manifest) {
            failed.error = std::move(manifest).error();
            return failed;
        }
        return buildPreview(std::move(manifest).value(), format);
    }

    auto ids = sharing::parseWorkshopIdList(text);
    if (!ids) {
        failed.error = std::move(ids).error();
        return failed;
    }
    if (idListGame.empty()) {
        failed.error = Error(ErrorCode::InvalidArgument,
                             "Select a game first: a Workshop ID list does not say which game it "
                             "is for.");
        return failed;
    }
    if (!m_ctx.adapterFor(idListGame)) {
        failed.error = cannotManage(format, idListGame);
        return failed;
    }
    auto manifest = sharing::manifestFromWorkshopIds(idListGame, ids.value());
    if (!manifest) {
        failed.error = std::move(manifest).error();
        return failed;
    }
    return buildPreview(std::move(manifest).value(), format);
}

ImportPreview ShareService::buildPreview(sharing::Manifest manifest,
                                         sharing::ImportTextFormat format) {
    ImportPreview preview;
    preview.format = format;
    const GameKey game = manifest.game;
    preview.gameName = gameDisplayName(game);
    if (!m_ctx.adapterFor(game)) {
        preview.error = cannotManage(format, game);
        return preview;
    }

    persistence::InstallationRepository installations(m_ctx.database);
    persistence::InventoryRepository inventory(m_ctx.database);
    persistence::PlaysetRepository playsets(m_ctx.database);
    auto installation = installations.get(game);
    auto mods = inventory.list(game);
    auto existing = playsets.listForGame(game);
    if (!installation || !mods || !existing) {
        const Error& error = !installation ? installation.error()
                             : !mods       ? mods.error()
                                           : existing.error();
        preview.error = Error(error.code,
                              "The import preview could not read the application "
                              "database. " +
                                  error.message,
                              error.detail);
        return preview;
    }

    if (installation.value()) {
        preview.gameInstalled = true;
        preview.installedGameVersion = installation.value()->version;
        preview.steamAppId = installation.value()->steamAppId;
    }
    if (preview.steamAppId.empty()) {
        if (const GameCatalogEntry* entry = findGame(game))
            preview.steamAppId = std::string(entry->steamAppId);
    }
    const std::vector<std::string> names = detail::names(existing.value());
    ImportPlan plan = buildImportPlan(
        game, manifest.playsetName, sharing::playsetEntriesFromManifest(manifest),
        detail::toMap(mods.value()), names, preview.installedGameVersion, manifest.gameVersionHint);
    if (!preview.gameInstalled) {
        plan.warnings.insert(
            plan.warnings.begin(),
            preview.gameName + " has not been found on this computer, so every "
                               "mod is treated as missing. The playset can still be saved; locate "
                               "or install the game, then rescan.");
    }
    preview.manifest = std::move(manifest);
    preview.plan = std::move(plan);
    return preview;
}

Result<AppliedImport> ShareService::applyImport(const ImportPreview& preview, ImportMode mode,
                                                std::optional<std::int64_t> targetPlaysetId,
                                                const std::string& name, ImportOptions options) {
    // --- Validation: nothing is written until all of it has passed. ---------
    if (!preview.ok())
        return notSaved(*preview.error);
    if (!preview.manifest)
        return makeError(ErrorCode::InvalidArgument,
                         "There is nothing to import. Paste a share code or a Workshop ID list "
                         "and preview it first.");
    const sharing::Manifest& manifest = *preview.manifest;
    if (Status valid = sharing::validateManifest(manifest); !valid)
        return notSaved(valid.error());
    const GameKey& game = manifest.game;
    if (!m_ctx.adapterFor(game))
        return cannotManage(preview.format, game);

    persistence::PlaysetRepository playsets(m_ctx.database);
    persistence::InstallationRepository installations(m_ctx.database);
    persistence::InventoryRepository inventory(m_ctx.database);
    auto existing = playsets.listForGame(game);
    if (!existing)
        return notSaved(existing.error());
    auto installation = installations.get(game);
    if (!installation)
        return notSaved(installation.error());
    auto mods = inventory.list(game);
    if (!mods)
        return notSaved(mods.error());

    // Rebuilt against the current inventory: a rescan since the preview may
    // have installed something, and installed content is never re-acquired.
    const std::vector<PlaysetEntry> requested = sharing::playsetEntriesFromManifest(manifest);
    const std::vector<std::string> names = detail::names(existing.value());
    ImportPlan plan =
        buildImportPlan(game, manifest.playsetName, requested, detail::toMap(mods.value()), names,
                        installation.value() ? installation.value()->version : std::string(),
                        manifest.gameVersionHint);
    const std::vector<PlaysetEntry> entries = plan.toPlayset().entries;
    if (entries != requested)
        return makeError(ErrorCode::Internal, "The import plan does not match the " +
                                                  textName(preview.format) +
                                                  "; nothing was saved.");

    AppliedImport applied;
    applied.mode = mode;
    std::string newName = detail::trimmed(name);
    Playset target;
    if (mode == ImportMode::New) {
        if (newName.empty()) {
            applied.playsetName = plan.proposedName;
        } else {
            // A typed name that is too long is refused, not silently cut.
            if (Status valid = checkPlaysetName(newName); !valid)
                return notSaved(valid.error());
            applied.playsetName = uniquePlaysetName(newName, names);
        }
        if (Status valid = checkPlaysetName(applied.playsetName); !valid)
            return notSaved(valid.error());
    } else {
        if (!targetPlaysetId)
            return makeError(ErrorCode::InvalidArgument,
                             "Choose the playset that the import should replace.");
        auto loaded = playsets.get(*targetPlaysetId);
        if (!loaded) {
            if (loaded.error().code == ErrorCode::NotFound)
                return makeError(ErrorCode::NotFound,
                                 "The playset chosen for replacement no longer exists; nothing "
                                 "was changed.");
            return notSaved(loaded.error());
        }
        target = std::move(loaded).value();
        if (target.game != game)
            return makeError(ErrorCode::WrongGame,
                             "The " + textName(preview.format) + " is for " +
                                 gameDisplayName(game) + ", but the chosen playset belongs to " +
                                 gameDisplayName(target.game) + "; nothing was changed.");
        applied.playsetName = target.name;
        if (!newName.empty() && newName != target.name) {
            if (Status valid = checkPlaysetName(newName); !valid)
                return notSaved(valid.error());
            for (const PlaysetSummary& other : existing.value()) {
                if (other.id != target.id && equalsIgnoringAsciiCase(other.name, newName))
                    return makeError(ErrorCode::AlreadyExists,
                                     "Another playset is already called \"" + other.name +
                                         "\"; nothing was changed.");
            }
            applied.playsetName = newName;
        }
    }

    // --- One transaction: playset, jobs and the active selection. -----------
    Status saved = m_ctx.database.transaction([&]() -> Status {
        persistence::AcquisitionJobRepository jobs(m_ctx.database);
        persistence::SettingsRepository settings(m_ctx.database);
        if (mode == ImportMode::New) {
            Playset playset;
            playset.game = game;
            playset.name = applied.playsetName;
            playset.entries = entries;
            auto id = playsets.create(playset);
            if (!id)
                return std::move(id).error();
            applied.playsetId = id.value();
        } else {
            auto history = playsets.replaceWithSnapshot(target.id, entries,
                                                        persistence::history_reason::ImportReplace);
            if (!history)
                return std::move(history).error();
            applied.replacedHistoryId = history.value();
            if (applied.playsetName != target.name)
                PMM_TRY(playsets.rename(target.id, applied.playsetName));
            applied.playsetId = target.id;
        }
        applied.jobs.clear();
        if (options.queueAcquisition) {
            for (const ModIdentity& identity : plan.toAcquire) {
                auto job = jobs.createOrReuse(identity, applied.playsetId);
                if (!job)
                    return std::move(job).error();
                applied.jobs.push_back(std::move(job).value());
            }
        }
        PMM_TRY(settings.setInt(settings_key::activePlayset(game), applied.playsetId));
        return settings.set(settings_key::LastGame, game);
    });
    if (!saved)
        return notSaved(saved.error());

    logInfo(kComponent, "share code imported",
            {{"game", game},
             {"format", preview.format == sharing::ImportTextFormat::ShareCode ? "code" : "list"},
             {"playset", std::to_string(applied.playsetId)},
             {"mode", mode == ImportMode::New ? "new" : "replace"},
             {"entries", std::to_string(entries.size())},
             {"jobs", std::to_string(applied.jobs.size())}});
    applied.plan = std::move(plan);
    return applied;
}

} // namespace pmm::services
