#include "core/import_plan.hpp"

#include "core/utf8.hpp"

#include <set>
#include <string_view>
#include <unordered_set>

namespace pmm {
namespace {

bool isAsciiSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string trimmed(std::string_view text) {
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    return std::string(text);
}

std::string folded(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// "v4.1.5", "V4.1.5" and "4.1.5" compare equal.
std::string normalizedVersion(std::string_view version) {
    std::string out = trimmed(version);
    if (!out.empty() && (out.front() == 'v' || out.front() == 'V'))
        out.erase(0, 1);
    return out;
}

std::string countText(std::size_t count, const char* singular, const char* plural) {
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

std::vector<ModIdentity> findLocalCandidates(const PlaysetEntry& entry,
                                             const InstalledModMap& inventory) {
    std::vector<ModIdentity> byFingerprint;
    std::vector<ModIdentity> byName;
    const std::string hint = trimmed(entry.nameHint);
    for (const auto& [identity, mod] : inventory) {
        if (identity.game != entry.identity.game || identity.source != source::Local ||
            mod.state == InstallState::Broken)
            continue;
        if (!entry.fingerprint.empty() && mod.fingerprint == entry.fingerprint)
            byFingerprint.push_back(identity);
        else if (!hint.empty() && trimmed(mod.descriptor.name) == hint)
            byName.push_back(identity);
    }
    byFingerprint.insert(byFingerprint.end(), byName.begin(), byName.end());
    return byFingerprint;
}

} // namespace

const char* toString(ImportEntryStatus status) noexcept {
    switch (status) {
    case ImportEntryStatus::Installed:
        return "installed";
    case ImportEntryStatus::Missing:
        return "missing";
    case ImportEntryStatus::LocalUnmapped:
        return "local_unmapped";
    case ImportEntryStatus::Broken:
        return "broken";
    }
    return "missing";
}

Playset ImportPlan::toPlayset() const {
    Playset playset;
    playset.game = game;
    playset.name = proposedName;
    playset.entries.reserve(entries.size());
    for (const ImportPlanEntry& item : entries)
        playset.entries.push_back(item.entry);
    return playset;
}

std::string uniquePlaysetName(std::string_view name, const std::vector<std::string>& existingNames,
                              std::string_view suffix, std::size_t maxBytes) {
    const std::string_view base = name.empty() ? std::string_view("Imported playset") : name;
    std::unordered_set<std::string> taken;
    for (const std::string& existing : existingNames)
        taken.insert(folded(existing));
    const auto candidate = [&](std::string_view counter) {
        const std::size_t tail = suffix.size() + counter.size();
        std::string_view stem = utf8Prefix(base, maxBytes > tail ? maxBytes - tail : 0);
        if (stem.size() < base.size()) {
            while (!stem.empty() && isAsciiSpace(stem.back()))
                stem.remove_suffix(1);
        }
        std::string out(stem);
        out.append(suffix);
        out.append(counter);
        return out;
    };
    std::string result = candidate({});
    for (std::size_t n = 2; taken.count(folded(result)) != 0; ++n)
        result = candidate(" (" + std::to_string(n) + ")");
    return result;
}

ImportPlan buildImportPlan(const GameKey& game, const std::string& playsetName,
                           const std::vector<PlaysetEntry>& requested,
                           const InstalledModMap& inventory,
                           const std::vector<std::string>& existingPlaysetNames,
                           const std::string& installedGameVersion,
                           const std::string& gameVersionHint) {
    ImportPlan plan;
    plan.game = game;
    plan.requestedName = playsetName;
    plan.proposedName = uniquePlaysetName(playsetName, existingPlaysetNames);
    plan.entries.reserve(requested.size());

    std::set<ModIdentity> seen;
    std::set<ModIdentity> queued;
    std::size_t versionMismatches = 0;
    std::size_t fingerprintMismatches = 0;
    std::size_t duplicates = 0;
    std::size_t otherGame = 0;

    for (const PlaysetEntry& entry : requested) {
        ImportPlanEntry item;
        item.entry = entry;
        if (!seen.insert(entry.identity).second)
            ++duplicates;
        if (entry.identity.game != game)
            ++otherGame;

        const auto found = inventory.find(entry.identity);
        if (found != inventory.end()) {
            const InstalledMod& mod = found->second;
            item.installedName = mod.descriptor.name;
            item.installedVersion = mod.descriptor.version;
            item.installedFingerprint = mod.fingerprint;
            if (mod.state == InstallState::Broken) {
                item.status = ImportEntryStatus::Broken;
                item.warnings.push_back("Installed but unusable; repair or reinstall it.");
                item.warnings.insert(item.warnings.end(), mod.problems.begin(), mod.problems.end());
            } else {
                item.status = ImportEntryStatus::Installed;
                if (mod.state == InstallState::NeedsUpdate)
                    item.warnings.push_back("An update is pending for the installed copy.");
                else if (mod.state == InstallState::Downloading)
                    item.warnings.push_back("The provider is currently updating the installed "
                                            "copy.");
            }
            if (!entry.versionHint.empty() && !mod.descriptor.version.empty() &&
                entry.versionHint != mod.descriptor.version) {
                ++versionMismatches;
                item.warnings.push_back("The shared setup used version " + entry.versionHint +
                                        "; the installed version is " + mod.descriptor.version +
                                        ".");
            }
            if (!entry.fingerprint.empty() && !mod.fingerprint.empty() &&
                entry.fingerprint != mod.fingerprint) {
                ++fingerprintMismatches;
                item.warnings.push_back(
                    "The installed files differ from the shared setup (different fingerprint).");
            }
        } else if (entry.identity.source == source::SteamWorkshop) {
            item.status = ImportEntryStatus::Missing;
            if (queued.insert(entry.identity).second)
                plan.toAcquire.push_back(entry.identity);
        } else {
            item.status = ImportEntryStatus::LocalUnmapped;
            item.localCandidates = findLocalCandidates(entry, inventory);
            item.warnings.push_back("Not installed here. Local mods cannot be downloaded; map it "
                                    "to an installed local mod or install it manually.");
        }

        ImportCounts& c = plan.counts;
        ++c.total;
        if (entry.enabled)
            ++c.enabled;
        switch (item.status) {
        case ImportEntryStatus::Installed:
            ++c.installed;
            break;
        case ImportEntryStatus::Missing:
            ++c.missing;
            ++(entry.enabled ? c.missingEnabled : c.missingDisabled);
            break;
        case ImportEntryStatus::LocalUnmapped:
            ++c.localUnmapped;
            break;
        case ImportEntryStatus::Broken:
            ++c.broken;
            break;
        }
        plan.entries.push_back(std::move(item));
    }

    const std::string hint = trimmed(gameVersionHint);
    if (!hint.empty()) {
        if (trimmed(installedGameVersion).empty())
            plan.warnings.push_back("The share code was created with game version " + hint +
                                    "; the installed game version could not be determined.");
        else if (normalizedVersion(hint) != normalizedVersion(installedGameVersion))
            plan.warnings.push_back("The share code was created with game version " + hint +
                                    ", but version " + trimmed(installedGameVersion) +
                                    " is installed. Mods may not work as expected.");
    }
    if (versionMismatches > 0)
        plan.warnings.push_back(countText(versionMismatches, "mod has", "mods have") +
                                " a different installed version than the shared setup.");
    if (fingerprintMismatches > 0)
        plan.warnings.push_back(countText(fingerprintMismatches, "mod's", "mods'") +
                                " installed files differ from the shared setup.");
    if (plan.counts.localUnmapped > 0)
        plan.warnings.push_back(
            countText(plan.counts.localUnmapped, "local mod is", "local mods are") +
            " not installed here and must be mapped or installed manually; "
            "local mods cannot be downloaded.");
    if (plan.counts.broken > 0)
        plan.warnings.push_back(countText(plan.counts.broken, "mod is", "mods are") +
                                " installed but broken; repair or reinstall before playing.");
    if (duplicates > 0)
        plan.warnings.push_back(countText(duplicates, "entry repeats", "entries repeat") +
                                " a mod that is already listed.");
    if (otherGame > 0)
        plan.warnings.push_back(countText(otherGame, "entry belongs", "entries belong") +
                                " to a different game.");
    plan.warnings.push_back(kIdenticalContentNotGuaranteed);
    return plan;
}

} // namespace pmm
