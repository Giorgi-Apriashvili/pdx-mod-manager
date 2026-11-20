#include "games/content_load_adapter.hpp"

#include "core/log.hpp"
#include "core/utf8.hpp"
#include "games/mod_metadata.hpp"
#include "games/workshop_content.hpp"
#include "platform/fs.hpp"
#include "platform/steam.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

namespace pmm::games {
namespace {

using platform::pathFromUtf8;
using platform::pathToUtf8;

constexpr std::uint64_t kMaxContentLoadBytes = 16ull * 1024 * 1024;
constexpr const char* kContentLoadFile = "content_load.json";
constexpr const char* kModFolder = "mod";

// The names behind one identity, for mergeSharedIdentities().
constexpr SharedIdentityNames kSharedFolders{"mod folders", &InstalledMod::contentPath};

// A non-empty run of ASCII digits.
bool allDigits(std::string_view digits) noexcept {
    return !digits.empty() &&
           std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// A folder (or archive) described by its own .metadata/metadata.json.
InstalledMod folderMod(ModIdentity identity, const fs::path& folder, std::string_view what) {
    InstalledMod mod;
    mod.identity = std::move(identity);
    mod.contentPath = folder;
    mod.descriptorPath = modMetadataPath(folder);
    mod.descriptorRef = contentLoadPath(folder);
    mod.contentUpdatedAt = platform::lastWriteUnixSeconds(folder);
    if (!isRegularFile(mod.descriptorPath)) {
        markBroken(mod, "The " + std::string(what) + " has no " + std::string(kModMetadataFile) +
                            ", so the game cannot load it.");
        return mod;
    }
    Result<ModMetadata> metadata = readModMetadata(mod.descriptorPath);
    if (!metadata) {
        markBroken(mod, metadata.error().message);
        return mod;
    }
    mod.descriptor = std::move(metadata->descriptor);
    // Reported by reverse engineering of 1.15.1, not seen here: the game logs
    // "Mod metadata read error" and leaves the mod out.
    if (!metadata->hasGameCustomData)
        mod.problems.push_back(
            "Its " + std::string(kModMetadataFile) +
            " has no \"game_custom_data\" entry; Victoria 3 reportedly leaves such a mod out.");
    return mod;
}

// A .zip in the mod folder: listed and launched by path, its metadata (inside
// the archive) not read.
InstalledMod archiveMod(ModIdentity identity, const fs::path& archive) {
    InstalledMod mod;
    mod.identity = std::move(identity);
    mod.contentPath = archive;
    mod.descriptorRef = contentLoadPath(archive);
    mod.descriptor.name = sanitizeUtf8(pathToUtf8(archive.stem()));
    mod.contentUpdatedAt = platform::lastWriteUnixSeconds(archive);
    return mod;
}

// The last component of a path as written in content_load.json.
std::string lastComponent(std::string_view text) {
    while (!text.empty() && (text.back() == '\\' || text.back() == '/'))
        text.remove_suffix(1);
    if (const std::size_t slash = text.find_last_of("/\\"); slash != std::string_view::npos)
        text.remove_prefix(slash + 1);
    return std::string(text);
}

// content_load.json with `paths` as enabledMods and every other key kept.
struct ContentLoadOutput {
    std::string contents;
    // The existing file already says the same (byte-identical, or the same
    // JSON in another layout): no write is needed.
    bool unchanged = false;
    std::vector<std::string> warnings;
};

// The existing content_load.json: nullopt when there is none, a JSON null
// when it is not a valid JSON object (it is then rebuilt).
Result<std::optional<nlohmann::ordered_json>> readContentLoad(const fs::path& file) {
    if (!isRegularFile(file))
        return std::optional<nlohmann::ordered_json>();
    Result<std::string> text = platform::readFile(file, kMaxContentLoadBytes);
    if (!text)
        return std::move(text).error();
    nlohmann::ordered_json document =
        nlohmann::ordered_json::parse(*text, nullptr, /*allow_exceptions=*/false);
    if (document.is_discarded() || !document.is_object())
        return std::optional<nlohmann::ordered_json>(nlohmann::ordered_json());
    return std::optional<nlohmann::ordered_json>(std::move(document));
}

// The path strings of the existing file's enabledMods, in file order.
std::vector<std::string> enabledPaths(const nlohmann::ordered_json& document) {
    std::vector<std::string> out;
    if (!document.is_object())
        return out;
    const auto enabled = document.find("enabledMods");
    if (enabled == document.end() || !enabled->is_array())
        return out;
    for (const auto& entry : *enabled) {
        if (!entry.is_object())
            continue;
        const auto path = entry.find("path");
        if (path != entry.end() && path->is_string())
            out.push_back(path->get<std::string>());
    }
    return out;
}

ContentLoadOutput composeContentLoad(const std::optional<nlohmann::ordered_json>& existingFile,
                                     const std::vector<std::string>& paths) {
    ContentLoadOutput out;
    nlohmann::ordered_json document;
    std::optional<nlohmann::ordered_json> existing;
    if (existingFile) {
        if (existingFile->is_object()) {
            document = *existingFile;
            existing = *existingFile;
        } else {
            out.warnings.push_back("content_load.json was not valid JSON and is rebuilt; DLC "
                                   "choices stored in it could not be preserved.");
        }
    }
    // The launcher's own layout of a new file; its schema allows no other
    // keys, so none is added.
    if (!document.is_object() || document.empty())
        document = nlohmann::ordered_json{{"enabledMods", nlohmann::ordered_json::array()},
                                          {"disabledDLC", nlohmann::ordered_json::array()},
                                          {"enabledUGC", nlohmann::ordered_json::array()}};
    nlohmann::ordered_json enabled = nlohmann::ordered_json::array();
    for (const std::string& path : paths)
        enabled.push_back(nlohmann::ordered_json{{"path", path}});
    if (document.contains("enabledMods")) {
        document["enabledMods"] = std::move(enabled);
    } else {
        nlohmann::ordered_json rebuilt{{"enabledMods", std::move(enabled)}};
        for (const auto& [key, value] : document.items())
            rebuilt[key] = value;
        document = std::move(rebuilt);
    }
    // As the launcher writes it: two-space indent, LF, no trailing newline.
    out.contents = document.dump(2, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
    out.unchanged = existing && *existing == document;
    return out;
}

std::string modLabel(const InstalledMod& mod) {
    const std::string& name = mod.descriptor.name;
    return "\"" + (name.empty() ? mod.identity.id : name) + "\"";
}

} // namespace

std::string contentLoadPath(const fs::path& path) {
    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);
    fs::path normal = (ec ? path : absolute).lexically_normal();
    if (!normal.has_filename() && normal.has_relative_path())
        normal = normal.parent_path(); // trailing separator
    std::string text = pathToUtf8(normal.make_preferred());
    if (text.size() >= 2 && text[1] == ':' && text[0] >= 'a' && text[0] <= 'z')
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    return text;
}

bool isParadoxModsFolder(std::string_view name) noexcept {
    constexpr std::string_view prefix = "pdx_";
    if (name.substr(0, prefix.size()) != prefix)
        return false;
    name.remove_prefix(prefix.size());
    const std::size_t separator = name.find('_');
    if (separator == std::string_view::npos)
        return allDigits(name);
    return allDigits(name.substr(0, separator)) && allDigits(name.substr(separator + 1));
}

ContentLoadJsonAdapter::ContentLoadJsonAdapter(const GameTraits& traits)
    : LauncherGameAdapter(traits) {}

Result<std::vector<InstalledMod>>
ContentLoadJsonAdapter::scanMods(const GameInstallation& installation) const {
    if (installation.userDataPath.empty())
        return makeError(ErrorCode::InvalidArgument,
                         "The " + std::string(catalog().displayName) +
                             " user data folder is not set. Rescan or choose it manually.");
    const GameKey game = std::string(traits().key);
    const fs::path modDir = installation.userDataPath / kModFolder;

    std::vector<InstalledMod> mods;
    std::size_t skipped = 0;
    for (const fs::path& folder : sortedEntries(modDir, true)) {
        const std::string name = pathToUtf8(folder.filename());
        if (isParadoxModsFolder(name)) {
            ++skipped;
            continue;
        }
        mods.push_back(folderMod(ModIdentity{game, source::Local, name}, folder, "mod folder"));
    }
    std::size_t archives = 0;
    for (const fs::path& file : sortedEntries(modDir, false)) {
        if (!hasExtension(file, ".zip"))
            continue;
        ++archives;
        mods.push_back(
            archiveMod(ModIdentity{game, source::Local, pathToUtf8(file.filename())}, file));
    }

    const WorkshopContent content =
        scanWorkshopContent(collectLibraries(installation, {}, traits().logComponent),
                            traits().steamAppId, traits().logComponent);
    for (const auto& [id, folder] : content.folders) {
        InstalledMod mod =
            folderMod(ModIdentity{game, source::SteamWorkshop, id}, folder, "Workshop folder");
        applyWorkshopState(mod, content);
        mods.push_back(std::move(mod));
    }

    mods = mergeSharedIdentities(std::move(mods), kSharedFolders, traits().logComponent);
    for (InstalledMod& mod : mods)
        dropUnstorablePaths(mod);
    logInfo(traits().logComponent, "Mod scan finished",
            {{"mods", std::to_string(mods.size())},
             {"archives", std::to_string(archives)},
             {"paradoxModsFolders", std::to_string(skipped)},
             {"workshopFolders", std::to_string(content.folders.size())}});
    return mods;
}

Result<std::vector<PlaysetEntry>>
ContentLoadJsonAdapter::readCurrentConfiguration(const GameInstallation& installation,
                                                 std::span<const InstalledMod> inventory) const {
    std::vector<PlaysetEntry> entries;
    const fs::path file = installation.userDataPath / kContentLoadFile;
    if (installation.userDataPath.empty() || !isRegularFile(file))
        return entries;
    Result<std::string> text = platform::readFile(file, kMaxContentLoadBytes);
    if (!text)
        return std::move(text).error();
    const nlohmann::json document = nlohmann::json::parse(*text, nullptr, false);
    if (document.is_discarded() || !document.is_object())
        return makeError(ErrorCode::ParseError,
                         "The game's mod configuration (content_load.json) is not valid JSON. "
                         "Start the game once from this app to rewrite it.",
                         pathToUtf8(file));
    const auto enabled = document.find("enabledMods");
    if (enabled == document.end() || enabled->is_null())
        return entries;
    if (!enabled->is_array())
        return makeError(ErrorCode::ParseError,
                         "The game's mod configuration (content_load.json) has an unexpected "
                         "format.",
                         pathToUtf8(file) + ": enabledMods is not a list");

    const GameKey game = std::string(traits().key);
    const fs::path modDir = installation.userDataPath / kModFolder;
    const auto findMod = [&](const auto& matches) -> const InstalledMod* {
        for (const InstalledMod& mod : inventory) {
            if (matches(mod))
                return &mod;
        }
        return nullptr;
    };

    std::set<ModIdentity> seen;
    for (const nlohmann::json& value : *enabled) {
        if (!value.is_object())
            continue;
        const auto pathValue = value.find("path");
        if (pathValue == value.end() || !pathValue->is_string())
            continue;
        const std::string written = sanitizeUtf8(pathValue->get<std::string>());
        const fs::path path = pathFromUtf8(written);
        PlaysetEntry entry;
        entry.enabled = true;
        const InstalledMod* mod = nullptr;
        if (std::optional<std::string> id = workshopIdFromPath(path, traits().steamAppId)) {
            // A Workshop item, also when Steam has not downloaded it (yet).
            entry.identity = ModIdentity{game, source::SteamWorkshop, std::move(*id)};
            mod = findMod([&](const InstalledMod& m) { return m.identity == entry.identity; });
        } else {
            mod = findMod([&](const InstalledMod& m) {
                return m.descriptorRef == written ||
                       (!m.contentPath.empty() && samePath(m.contentPath, path));
            });
            if (mod != nullptr) {
                entry.identity = mod->identity;
            } else {
                entry.identity = ModIdentity{game, source::Local, lastComponent(written)};
                const std::string& name = entry.identity.id;
                if (name.empty() || name == "." || name == ".." ||
                    name.find(':') != std::string::npos) {
                    // e.g. "" or a drive root: no folder name to identify a mod by.
                    logWarning(traits().logComponent,
                               "An enabled mod's path names no mod folder; it is left out",
                               {{"path", written}});
                    continue;
                }
                const bool inModFolder = path.is_absolute() && samePath(path.parent_path(), modDir);
                if (!inModFolder) {
                    // Elsewhere, with the name of an installed mod: that identity
                    // means the installed folder, so taking it would swap in a
                    // different mod. The same folder reached by another route
                    // (a junction, a short name) is that mod.
                    mod = findMod(
                        [&](const InstalledMod& m) { return m.identity == entry.identity; });
                    std::error_code ec;
                    if (mod != nullptr &&
                        (mod->contentPath.empty() || !fs::equivalent(path, mod->contentPath, ec))) {
                        logWarning(traits().logComponent,
                                   "An enabled mod outside the mod folder and the Workshop has "
                                   "the folder name of an installed mod; it is left out",
                                   {{"path", written}, {"id", name}});
                        continue;
                    }
                    if (mod == nullptr)
                        logInfo(traits().logComponent,
                                "An enabled mod lies outside the mod folder and the Workshop; "
                                "it is listed as a local mod that is not installed",
                                {{"path", written}});
                }
            }
        }
        if (mod != nullptr) {
            entry.nameHint = mod->descriptor.name;
            entry.versionHint = mod->descriptor.version;
            entry.fingerprint = mod->fingerprint;
        } else if (isRegularFile(modMetadataPath(path))) {
            // Not in the inventory (e.g. a library this scan does not know):
            // its own metadata still names it.
            if (Result<ModMetadata> metadata = readModMetadata(modMetadataPath(path))) {
                entry.nameHint = metadata->descriptor.name;
                entry.versionHint = metadata->descriptor.version;
            }
        }
        if (entry.nameHint.empty())
            entry.nameHint = lastComponent(written);
        if (seen.insert(entry.identity).second)
            entries.push_back(std::move(entry));
    }
    return entries;
}

Result<LaunchPlan>
ContentLoadJsonAdapter::buildLaunchPlan(const GameInstallation& installation,
                                        std::span<const InstalledMod> orderedEnabledMods) const {
    Result<LaunchPlan> begun = beginLaunchPlan(installation);
    if (!begun)
        return begun;
    LaunchPlan plan = std::move(begun).value();
    const fs::path file = installation.userDataPath / kContentLoadFile;
    Result<std::optional<nlohmann::ordered_json>> existing = readContentLoad(file);
    if (!existing)
        return std::move(existing).error(); // never clobber DLC choices we could not read
    const std::vector<std::string> written =
        *existing ? enabledPaths(**existing) : std::vector<std::string>();

    std::vector<std::string> paths;
    std::set<ModIdentity> seen;
    for (const InstalledMod& mod : orderedEnabledMods) {
        const std::string label = modLabel(mod);
        if (mod.identity.game != traits().key)
            return makeError(ErrorCode::WrongGame, "The mod " + label + " belongs to another game.",
                             toString(mod.identity));
        if (!seen.insert(mod.identity).second)
            return makeError(ErrorCode::InvalidArgument, "The mod " + label + " is listed twice.",
                             toString(mod.identity));
        if (mod.identity.source != source::Local &&
            (mod.identity.source != source::SteamWorkshop || !isWorkshopId(mod.identity.id)))
            return makeError(ErrorCode::InvalidArgument,
                             "The mod " + label + " has an unsupported source.",
                             toString(mod.identity));
        std::error_code ec;
        if (mod.contentPath.empty() || !fs::exists(mod.contentPath, ec))
            return makeError(ErrorCode::NotFound,
                             "The files of the mod " + label +
                                 " are missing. Rescan or reinstall it.",
                             toString(mod.identity) + ": " + pathToUtf8(mod.contentPath));
        if (!platform::isUtf8Representable(mod.contentPath))
            return makeError(ErrorCode::ValidationFailed,
                             "The location of the mod " + label +
                                 " is not valid Unicode and cannot be listed in "
                                 "content_load.json. Rename it, then rescan.",
                             toString(mod.identity));
        // The file's own spelling of this location, else the launcher's form.
        const auto reused = std::find_if(written.begin(), written.end(), [&](const std::string& p) {
            return samePath(pathFromUtf8(p), mod.contentPath);
        });
        paths.push_back(reused != written.end() ? *reused : contentLoadPath(mod.contentPath));
    }

    ContentLoadOutput contentLoad = composeContentLoad(*existing, paths);
    plan.warnings = std::move(contentLoad.warnings);
    if (!contentLoad.unchanged)
        plan.configWrites.push_back(
            ConfigWrite{file, std::move(contentLoad.contents), "load order (content_load.json)"});
    return plan;
}

Status ContentLoadJsonAdapter::verifyWorkshopContent(const fs::path& installPath) const {
    if (std::optional<Status> verdict = checkWorkshopContentPath(installPath))
        return std::move(*verdict);
    const fs::path metadata = modMetadataPath(installPath);
    if (!isRegularFile(metadata))
        return makeError(ErrorCode::ValidationFailed,
                         std::string(kModMetadataFile) + " is missing from the downloaded content",
                         pathToUtf8(installPath));
    PMM_TRY(requireNonEmptyFile(metadata, kModMetadataFile, installPath));
    // The scan's rule: a file that is not a JSON object leaves the mod Broken.
    Result<ModMetadata> parsed = readModMetadata(metadata);
    if (!parsed)
        return makeError(ErrorCode::ValidationFailed,
                         std::string(kModMetadataFile) +
                             (parsed.error().code == ErrorCode::ParseError
                                  ? " is not a valid JSON object"
                                  : " cannot be read"),
                         parsed.error().detail);
    return {};
}

} // namespace pmm::games
