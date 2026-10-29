#include "services/share_service.hpp"

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
#include <map>
#include <utility>

namespace pmm::services {
namespace {

constexpr const char* kComponent = "services.share";
constexpr const char* kFallbackPlaysetName = "Shared playset";

std::size_t utf8Length(unsigned char lead) {
    if (lead < 0x80)
        return 1;
    if ((lead >> 5) == 0x6)
        return 2;
    if ((lead >> 4) == 0xE)
        return 3;
    return 4;
}

// Display text fitted to the manifest rules: well-formed UTF-8 (otherwise
// dropped), control characters replaced by spaces, trimmed, and cut on a
// character boundary to at most `maxBytes`.
std::string fitHint(std::string_view text, std::size_t maxBytes) {
    if (!sharing::isValidUtf8(text))
        return {};
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t length = std::min(utf8Length(lead), text.size() - i);
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

} // namespace

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

ImportPreview ShareService::previewImport(std::string_view text) {
    ImportPreview preview;
    auto manifest = sharing::decodeShareCode(text);
    if (!manifest) {
        preview.error = std::move(manifest).error();
        return preview;
    }
    const GameKey game = manifest.value().game;
    preview.gameName = gameDisplayName(game);
    if (!m_ctx.adapterFor(game)) {
        preview.error = Error(ErrorCode::UnsupportedGame,
                              "This share code is for " + preview.gameName +
                                  ", which this build of Paradox Mod Manager cannot manage.");
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
    }
    const std::vector<std::string> names = detail::names(existing.value());
    ImportPlan plan = buildImportPlan(
        game, manifest.value().playsetName, sharing::playsetEntriesFromManifest(manifest.value()),
        detail::toMap(mods.value()), names, preview.installedGameVersion,
        manifest.value().gameVersionHint);
    if (!preview.gameInstalled) {
        plan.warnings.insert(
            plan.warnings.begin(),
            preview.gameName + " has not been found on this computer, so every "
                               "mod is treated as missing. The playset can still be saved; locate "
                               "or install the game, then rescan.");
    }
    preview.manifest = std::move(manifest).value();
    preview.plan = std::move(plan);
    return preview;
}

Result<AppliedImport> ShareService::applyImport(const ImportPreview& preview, ImportMode mode,
                                                std::optional<std::int64_t> targetPlaysetId,
                                                const std::string& name) {
    // --- Validation: nothing is written until all of it has passed. ---------
    if (!preview.ok())
        return notSaved(*preview.error);
    if (!preview.manifest)
        return makeError(ErrorCode::InvalidArgument,
                         "There is nothing to import. Paste a share code and preview it first.");
    const sharing::Manifest& manifest = *preview.manifest;
    if (Status valid = sharing::validateManifest(manifest); !valid)
        return notSaved(valid.error());
    const GameKey& game = manifest.game;
    if (!m_ctx.adapterFor(game))
        return makeError(ErrorCode::UnsupportedGame,
                         "This share code is for " + gameDisplayName(game) +
                             ", which this build of Paradox Mod Manager cannot manage.");

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
        return makeError(ErrorCode::Internal,
                         "The import plan does not match the share code; nothing was saved.");

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
                             "The share code is for " + gameDisplayName(game) +
                                 ", but the chosen playset belongs to " +
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
        for (const ModIdentity& identity : plan.toAcquire) {
            auto job = jobs.createOrReuse(identity, applied.playsetId);
            if (!job)
                return std::move(job).error();
            applied.jobs.push_back(std::move(job).value());
        }
        PMM_TRY(settings.setInt(settings_key::activePlayset(game), applied.playsetId));
        return settings.set(settings_key::LastGame, game);
    });
    if (!saved)
        return notSaved(saved.error());

    logInfo(kComponent, "share code imported",
            {{"game", game},
             {"playset", std::to_string(applied.playsetId)},
             {"mode", mode == ImportMode::New ? "new" : "replace"},
             {"entries", std::to_string(entries.size())},
             {"jobs", std::to_string(applied.jobs.size())}});
    applied.plan = std::move(plan);
    return applied;
}

} // namespace pmm::services
