#include "games/dlc_load_adapter.hpp"

#include "core/log.hpp"
#include "core/utf8.hpp"
#include "games/descriptor.hpp"
#include "games/workshop_content.hpp"
#include "platform/fs.hpp"
#include "platform/steam.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <system_error>
#include <utility>

namespace pmm::games {
namespace {

namespace steam = platform::steam;
using platform::pathFromUtf8;
using platform::pathToUtf8;
using platform::toGenericUtf8;
using steam::keyEquals;

constexpr std::uint64_t kMaxDlcLoadBytes = 16ull * 1024 * 1024;
constexpr const char* kDlcLoadFile = "dlc_load.json";
constexpr const char* kModFolder = "mod";
constexpr const char* kContentDescriptor = "descriptor.mod";

// "ugc_123.mod" -> "123"
std::optional<std::string> ugcIdFromFileName(std::string_view name) {
    constexpr std::string_view prefix = "ugc_";
    constexpr std::string_view suffix = ".mod";
    if (name.size() <= prefix.size() + suffix.size() || !keyEquals(name.substr(0, 4), prefix) ||
        !keyEquals(name.substr(name.size() - suffix.size()), suffix))
        return std::nullopt;
    std::string id(name.substr(prefix.size(), name.size() - prefix.size() - suffix.size()));
    if (!isWorkshopId(id))
        return std::nullopt;
    return id;
}

std::string ugcReference(std::string_view id) {
    return std::string(kModFolder) + "/ugc_" + std::string(id) + ".mod";
}

// Absolute, normal form of a path declared in a descriptor: absolute as
// written, otherwise relative to the user-data folder (older descriptors use
// path="mod/<folder>").
fs::path resolveDeclared(std::string_view declared, const fs::path& userData) {
    fs::path path = pathFromUtf8(declared);
    if (!path.is_absolute())
        path = userData / path;
    return path.lexically_normal();
}

// First .zip directly inside a legacy Workshop folder.
std::optional<fs::path> legacyArchive(const fs::path& folder) {
    for (const fs::path& file : sortedEntries(folder, false)) {
        if (hasExtension(file, ".zip"))
            return file;
    }
    return std::nullopt;
}

// The *.mod files directly inside a folder, in file name order.
std::vector<fs::path> modFilesIn(const fs::path& folder) {
    std::vector<fs::path> files = sortedEntries(folder, false);
    std::erase_if(files, [](const fs::path& file) { return !hasExtension(file, ".mod"); });
    return files;
}

// Why a Workshop folder without a legacy archive has no content descriptor.
std::string missingContentDescriptor(const fs::path& folder) {
    const std::vector<fs::path> files = modFilesIn(folder);
    if (files.size() < 2)
        return "The Workshop folder has no descriptor.mod.";
    std::string names;
    for (std::size_t i = 0; i < files.size(); ++i) {
        names += i == 0 ? "" : i + 1 == files.size() ? " and " : ", ";
        names += sanitizeUtf8(pathToUtf8(files[i].filename()));
    }
    return "The Workshop folder has no descriptor.mod, and its " + std::to_string(files.size()) +
           " .mod files (" + names + ") leave it open which one describes the mod.";
}

// A registered descriptor in <user data>/mod.
struct Registration {
    fs::path file;
    std::string ref;
    std::optional<DescriptorDocument> document; // nullopt when unreadable
    std::string error;
    ModDescriptor descriptor;
    fs::path declared; // resolved path= (or archive=); empty when neither is set
    bool declaredArchive = false;
    std::optional<std::string> workshopId;
    fs::path library; // library root inferred from `declared`, if any
};

Registration readRegistration(const fs::path& file, const fs::path& userData,
                              std::string_view appId) {
    Registration r;
    r.file = file;
    r.ref = descriptorReference(file);
    const std::string name = pathToUtf8(file.filename());
    Result<DescriptorDocument> document = readDescriptorDocument(file);
    if (!document) {
        r.error = document.error().message;
        // Unverifiable without remote_file_id, but this is the launcher's
        // naming convention for Workshop registrations.
        r.workshopId = ugcIdFromFileName(name);
        return r;
    }
    r.descriptor = toModDescriptor(*document);
    r.document = std::move(document).value();
    if (!r.descriptor.path.empty()) {
        r.declared = resolveDeclared(r.descriptor.path, userData);
    } else if (!r.descriptor.archive.empty()) {
        r.declared = resolveDeclared(r.descriptor.archive, userData);
        r.declaredArchive = true;
    }
    if (!r.declared.empty())
        r.workshopId = workshopIdFromPath(r.declared, appId, &r.library);
    if (!r.workshopId) {
        auto id = ugcIdFromFileName(name);
        if (id && r.descriptor.remoteFileId == *id)
            r.workshopId = std::move(id);
    }
    return r;
}

ModIdentity identityOf(const Registration& r, const GameKey& game) {
    if (r.workshopId)
        return ModIdentity{game, source::SteamWorkshop, *r.workshopId};
    return ModIdentity{game, source::Local, pathToUtf8(r.file.stem())};
}

bool declaredContentExists(const Registration& reg) {
    return reg.declaredArchive ? isRegularFile(reg.declared) : isDirectory(reg.declared);
}

// A Workshop item whose content is on disk: `folder` is its folder in a Steam
// library, or nullptr when only the registration's own path/archive (which
// exists) has it, e.g. a copy in a custom folder.
InstalledMod workshopMod(const GameKey& game, const std::string& id, const Registration* reg,
                         const fs::path* folder, const WorkshopContent& content) {
    assert(folder != nullptr || reg != nullptr);
    InstalledMod mod;
    mod.identity = ModIdentity{game, source::SteamWorkshop, id};

    fs::path contentPath;
    std::optional<ModDescriptor> contentDescriptor;
    std::string contentProblem;
    if (folder != nullptr) {
        contentPath = *folder;
        // A registration whose path= names the folder keeps it as the content,
        // also when a stray .zip lies in it.
        const bool folderRegistered = reg != nullptr && !reg->declaredArchive &&
                                      !reg->declared.empty() && samePath(reg->declared, *folder);
        if (reg != nullptr && reg->declaredArchive && isRegularFile(reg->declared) &&
            platform::isWithin(reg->declared, *folder)) {
            contentPath = reg->declared;
        } else if (const std::optional<fs::path> descriptorFile = contentDescriptorFile(*folder)) {
            Result<ModDescriptor> parsed = readDescriptor(*descriptorFile);
            if (parsed)
                contentDescriptor = std::move(parsed).value();
            else
                contentProblem = parsed.error().message;
        } else if (auto archive = folderRegistered ? std::nullopt : legacyArchive(*folder)) {
            contentPath = *archive;
            contentProblem = "The legacy (zipped) Workshop item has no descriptor.mod outside "
                             "its archive.";
        } else {
            contentProblem = missingContentDescriptor(*folder);
        }
    } else {
        contentPath = reg->declared;
    }
    mod.contentPath = contentPath;

    if (reg != nullptr) {
        mod.descriptorPath = reg->file;
        mod.descriptorRef = reg->ref;
        if (reg->document) {
            mod.descriptor = reg->descriptor;
        } else if (contentDescriptor) {
            mod.descriptor = *contentDescriptor;
            mod.problems.push_back("The registered descriptor " + reg->ref +
                                   " could not be read; it is rewritten when the game is launched "
                                   "from this app.");
        } else {
            markBroken(mod, reg->error);
        }
    } else {
        mod.descriptorRef = ugcReference(id);
        if (contentDescriptor)
            mod.descriptor = *contentDescriptor;
        else
            markBroken(mod, contentProblem);
    }

    if (reg != nullptr && reg->document &&
        (reg->declared.empty() || !samePath(reg->declared, contentPath))) {
        mod.problems.push_back("The registered descriptor " + reg->ref +
                               " points to another location; it is rewritten when the game is "
                               "launched from this app.");
    }

    applyWorkshopState(mod, content);
    return mod;
}

InstalledMod localMod(const GameKey& game, const Registration& reg) {
    InstalledMod mod;
    mod.identity = identityOf(reg, game);
    mod.descriptorPath = reg.file;
    mod.descriptorRef = reg.ref;
    if (!reg.document) {
        markBroken(mod, reg.error);
        return mod;
    }
    mod.descriptor = reg.descriptor;
    if (reg.declared.empty()) {
        markBroken(mod, "The descriptor " + reg.ref + " has no path or archive entry.");
        return mod;
    }
    mod.contentPath = reg.declared;
    if (!declaredContentExists(reg))
        markBroken(mod, std::string(reg.declaredArchive ? "The mod archive " : "The mod folder ") +
                            toGenericUtf8(reg.declared) + " does not exist.");
    mod.contentUpdatedAt = platform::lastWriteUnixSeconds(reg.declared);
    return mod;
}

// "mod/<file>" (prefix compared case-insensitively, as Windows would).
bool isPlainModReference(std::string_view ref) {
    const std::string prefix = std::string(kModFolder) + "/";
    if (ref.size() <= prefix.size() || !keyEquals(ref.substr(0, prefix.size()), prefix))
        return false;
    const std::string_view name = ref.substr(prefix.size());
    return name.find_first_of("/\\:") == std::string_view::npos && name != "." && name != "..";
}

std::string referenceStem(std::string_view ref) {
    std::string_view name = ref;
    if (const std::size_t slash = name.find_last_of("/\\"); slash != std::string_view::npos)
        name.remove_prefix(slash + 1);
    if (name.size() > 4 && keyEquals(name.substr(name.size() - 4), ".mod"))
        name.remove_suffix(4);
    return std::string(name.empty() ? ref : name);
}

// dlc_load.json with `refs` as enabled_mods and every other key preserved.
struct DlcLoadOutput {
    std::string contents;
    // The existing file already says the same (byte-identical, or the same
    // JSON in another layout): no write is needed.
    bool unchanged = false;
    std::vector<std::string> warnings;
};

Result<DlcLoadOutput> composeDlcLoad(const fs::path& file, const std::vector<std::string>& refs) {
    DlcLoadOutput out;
    nlohmann::ordered_json document;
    std::optional<nlohmann::ordered_json> existing;
    if (isRegularFile(file)) {
        Result<std::string> text = platform::readFile(file, kMaxDlcLoadBytes);
        if (!text)
            return std::move(text).error(); // never clobber DLC choices we could not read
        document = nlohmann::ordered_json::parse(*text, nullptr, false);
        if (document.is_discarded() || !document.is_object()) {
            out.warnings.push_back("dlc_load.json was not valid JSON and is rebuilt; DLC choices "
                                   "stored in it could not be preserved.");
            document = nlohmann::ordered_json();
        } else {
            existing = document;
        }
    }
    if (!document.is_object() || document.empty())
        document = nlohmann::ordered_json{{"enabled_mods", nlohmann::ordered_json::array()},
                                          {"disabled_dlcs", nlohmann::ordered_json::array()}};
    if (document.contains("enabled_mods")) {
        document["enabled_mods"] = refs;
    } else {
        nlohmann::ordered_json rebuilt{{"enabled_mods", refs}};
        for (const auto& [key, value] : document.items())
            rebuilt[key] = value;
        document = std::move(rebuilt);
    }
    out.contents = document.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
    out.unchanged = existing && *existing == document;
    return out;
}

// True when `file` exists with exactly `contents` (no write needed).
bool fileHolds(const fs::path& file, std::string_view contents) {
    if (!isRegularFile(file))
        return false;
    Result<std::string> current = platform::readFile(file);
    return current && *current == contents;
}

std::string modLabel(const InstalledMod& mod) {
    const std::string& name = mod.descriptor.name;
    return "\"" + (name.empty() ? mod.identity.id : name) + "\"";
}

// The names behind one identity, for mergeSharedIdentities().
constexpr SharedIdentityNames kSharedDescriptors{"descriptor files", &InstalledMod::descriptorPath};

} // namespace

std::string descriptorReference(const fs::path& descriptorFile) {
    return std::string(kModFolder) + "/" + pathToUtf8(descriptorFile.filename());
}

std::optional<fs::path> contentDescriptorFile(const fs::path& folder) {
    fs::path descriptor = folder / kContentDescriptor;
    if (isRegularFile(descriptor))
        return descriptor;
    // A legacy zipped item keeps its descriptor inside the archive; a .mod
    // next to the archive may describe the archive, not the folder.
    if (legacyArchive(folder))
        return std::nullopt;
    std::vector<fs::path> files = modFilesIn(folder);
    if (files.size() != 1)
        return std::nullopt;
    return std::move(files.front());
}

DlcLoadJsonAdapter::DlcLoadJsonAdapter(const GameTraits& traits) : LauncherGameAdapter(traits) {}

Result<std::vector<InstalledMod>>
DlcLoadJsonAdapter::scanMods(const GameInstallation& installation) const {
    if (installation.userDataPath.empty())
        return makeError(ErrorCode::InvalidArgument,
                         "The " + std::string(catalog().displayName) +
                             " user data folder is not set. Rescan or choose it manually.");
    const GameKey game = std::string(traits().key);
    const std::string appId(traits().steamAppId);

    std::vector<Registration> registrations;
    std::vector<fs::path> inferredLibraries;
    for (const fs::path& file : sortedEntries(installation.userDataPath / kModFolder, false)) {
        if (!hasExtension(file, ".mod"))
            continue;
        registrations.push_back(readRegistration(file, installation.userDataPath, appId));
        if (!registrations.back().library.empty())
            inferredLibraries.push_back(registrations.back().library);
    }
    const WorkshopContent content = scanWorkshopContent(
        collectLibraries(installation, inferredLibraries, traits().logComponent), appId,
        traits().logComponent);

    std::vector<InstalledMod> mods;
    std::size_t notDownloaded = 0;
    // Workshop registrations grouped by id; ugc_<id>.mod is the canonical one.
    std::map<std::string, std::vector<const Registration*>> byWorkshopId;
    for (const Registration& reg : registrations) {
        if (reg.workshopId)
            byWorkshopId[*reg.workshopId].push_back(&reg);
        else
            mods.push_back(localMod(game, reg));
    }
    for (const auto& [id, regs] : byWorkshopId) {
        const std::string canonical = "ugc_" + id + ".mod";
        const auto primary = std::find_if(regs.begin(), regs.end(), [&](const Registration* r) {
            return keyEquals(pathToUtf8(r->file.filename()), canonical);
        });
        const Registration* chosen = primary != regs.end() ? *primary : regs.front();
        const auto folder = content.folders.find(id);
        if (folder == content.folders.end() && !declaredContentExists(*chosen)) {
            // Registered but not downloaded (e.g. after a reinstall): not an
            // installed mod, so that playsets see it as missing and it can be
            // acquired. Noted for diagnostics only.
            ++notDownloaded;
            logInfo(traits().logComponent,
                    "A registered Workshop item is not downloaded; it is not part of the inventory",
                    {{"id", id},
                     {"descriptor", chosen->ref},
                     {"steam", steamStateNote(content.findItem(id))}});
            continue;
        }
        InstalledMod mod = workshopMod(
            game, id, chosen, folder != content.folders.end() ? &folder->second : nullptr, content);
        for (const Registration* other : regs) {
            if (other != chosen)
                mod.problems.push_back("Also registered as " + other->ref + ".");
        }
        mods.push_back(std::move(mod));
    }
    for (const auto& [id, folder] : content.folders) {
        if (byWorkshopId.count(id) == 0)
            mods.push_back(workshopMod(game, id, nullptr, &folder, content));
    }
    mods = mergeSharedIdentities(std::move(mods), kSharedDescriptors, traits().logComponent);
    for (InstalledMod& mod : mods)
        dropUnstorablePaths(mod);
    logInfo(traits().logComponent, "Mod scan finished",
            {{"mods", std::to_string(mods.size())},
             {"registered", std::to_string(registrations.size())},
             {"workshopFolders", std::to_string(content.folders.size())},
             {"registeredNotDownloaded", std::to_string(notDownloaded)}});
    return mods;
}

Result<std::vector<PlaysetEntry>>
DlcLoadJsonAdapter::readCurrentConfiguration(const GameInstallation& installation,
                                             std::span<const InstalledMod> inventory) const {
    std::vector<PlaysetEntry> entries;
    const fs::path file = installation.userDataPath / kDlcLoadFile;
    if (installation.userDataPath.empty() || !isRegularFile(file))
        return entries;
    Result<std::string> text = platform::readFile(file, kMaxDlcLoadBytes);
    if (!text)
        return std::move(text).error();
    const nlohmann::json document = nlohmann::json::parse(*text, nullptr, false);
    if (document.is_discarded() || !document.is_object())
        return makeError(ErrorCode::ParseError,
                         "The game's mod configuration (dlc_load.json) is not valid JSON. Start "
                         "the game once from this app to rewrite it.",
                         pathToUtf8(file));
    const auto enabled = document.find("enabled_mods");
    if (enabled == document.end() || enabled->is_null())
        return entries;
    if (!enabled->is_array())
        return makeError(ErrorCode::ParseError,
                         "The game's mod configuration (dlc_load.json) has an unexpected format.",
                         pathToUtf8(file) + ": enabled_mods is not a list");

    const GameKey game = std::string(traits().key);
    const auto findByRef = [&](std::string_view ref) -> const InstalledMod* {
        for (const InstalledMod& mod : inventory) {
            if (mod.descriptorRef == ref)
                return &mod;
        }
        for (const InstalledMod& mod : inventory) {
            if (keyEquals(mod.descriptorRef, ref))
                return &mod;
        }
        return nullptr;
    };
    const auto findByIdentity = [&](const ModIdentity& identity) -> const InstalledMod* {
        for (const InstalledMod& mod : inventory) {
            if (mod.identity == identity)
                return &mod;
        }
        return nullptr;
    };
    // The registration that a "mod/<file>" reference names, if the file exists.
    const auto registrationAt = [&](std::string_view ref) -> std::optional<Registration> {
        if (!isPlainModReference(ref))
            return std::nullopt;
        const fs::path descriptorFile = installation.userDataPath / pathFromUtf8(ref);
        if (!isRegularFile(descriptorFile))
            return std::nullopt;
        return readRegistration(descriptorFile, installation.userDataPath, traits().steamAppId);
    };

    std::set<ModIdentity> seen;
    for (const nlohmann::json& value : *enabled) {
        if (!value.is_string())
            continue;
        std::string ref = sanitizeUtf8(value.get<std::string>());
        std::replace(ref.begin(), ref.end(), '\\', '/');
        PlaysetEntry entry;
        const InstalledMod* mod = findByRef(ref);
        const bool byRef = mod != nullptr;
        std::string hint = ref;
        std::string versionHint;
        std::optional<ModIdentity> workshop; // a Workshop item that is not installed
        if (mod == nullptr && isPlainModReference(ref)) {
            // Another registration of an inventory mod (e.g. a second .mod for
            // the same Workshop item), or the registration of a Workshop item
            // that is not downloaded: resolve it through its own descriptor.
            if (const std::optional<Registration> reg = registrationAt(ref)) {
                const ModIdentity identity = identityOf(*reg, game);
                mod = findByIdentity(identity);
                if (identity.source == source::SteamWorkshop)
                    workshop = identity;
                if (reg->document && !reg->descriptor.name.empty())
                    hint = reg->descriptor.name;
                if (reg->document)
                    versionHint = reg->descriptor.version;
            } else if (auto id =
                           ugcIdFromFileName(ref.substr(std::string_view(kModFolder).size() + 1))) {
                // No descriptor at all: the launcher's naming convention.
                workshop = ModIdentity{game, source::SteamWorkshop, *id};
                mod = findByIdentity(*workshop);
            }
        }
        if (mod != nullptr) {
            entry.identity = mod->identity;
            entry.nameHint = mod->descriptor.name;
            entry.versionHint = mod->descriptor.version;
            entry.fingerprint = mod->fingerprint;
            // No descriptor names the mod (e.g. several descriptor files give
            // it one id): the file the game refers to does, else the
            // reference, so that the entry keeps a name after a rename.
            if (entry.nameHint.empty() && byRef) {
                const std::optional<Registration> reg = registrationAt(ref);
                if (reg && reg->document && !reg->descriptor.name.empty())
                    hint = reg->descriptor.name;
            }
            if (entry.nameHint.empty())
                entry.nameHint = hint;
        } else if (workshop) {
            entry.identity = *workshop;
            entry.nameHint = hint;
            entry.versionHint = versionHint;
        } else {
            entry.identity = ModIdentity{game, source::Local, referenceStem(ref)};
            entry.nameHint = hint;
        }
        entry.enabled = true;
        if (seen.insert(entry.identity).second)
            entries.push_back(std::move(entry));
    }
    return entries;
}

Result<LaunchPlan>
DlcLoadJsonAdapter::buildLaunchPlan(const GameInstallation& installation,
                                    std::span<const InstalledMod> orderedEnabledMods) const {
    Result<LaunchPlan> begun = beginLaunchPlan(installation);
    if (!begun)
        return begun;
    LaunchPlan plan = std::move(begun).value();
    const fs::path modDir = installation.userDataPath / kModFolder;

    std::vector<std::string> refs;
    std::set<ModIdentity> seen;
    for (const InstalledMod& mod : orderedEnabledMods) {
        const std::string label = modLabel(mod);
        if (mod.identity.game != traits().key)
            return makeError(ErrorCode::WrongGame, "The mod " + label + " belongs to another game.",
                             toString(mod.identity));
        if (!seen.insert(mod.identity).second)
            return makeError(ErrorCode::InvalidArgument, "The mod " + label + " is listed twice.",
                             toString(mod.identity));

        std::error_code ec;
        if (mod.contentPath.empty() || !fs::exists(mod.contentPath, ec))
            return makeError(ErrorCode::NotFound,
                             "The files of the mod " + label +
                                 " are missing. Rescan or reinstall "
                                 "it.",
                             toString(mod.identity) + ": " + pathToUtf8(mod.contentPath));

        // References name the file only ("mod/<file>"), so a descriptor must
        // lie directly in the mod folder.
        if (mod.identity.source == source::Local) {
            if (mod.descriptorPath.empty() || !isRegularFile(mod.descriptorPath) ||
                !samePath(mod.descriptorPath.parent_path(), modDir))
                return makeError(ErrorCode::NotFound,
                                 "The local mod " + label +
                                     " has no descriptor in the game's mod "
                                     "folder. Reinstall it, then rescan.",
                                 toString(mod.identity) + ": " + pathToUtf8(mod.descriptorPath));
            refs.push_back(descriptorReference(mod.descriptorPath));
            continue;
        }
        if (mod.identity.source != source::SteamWorkshop || !isWorkshopId(mod.identity.id))
            return makeError(ErrorCode::InvalidArgument,
                             "The mod " + label + " has an unsupported source.",
                             toString(mod.identity));

        // A registration is reused when it points at the actual content;
        // otherwise mod/ugc_<id>.mod is (re)generated.
        const bool archive = isRegularFile(mod.contentPath);
        const fs::path ugcFile = modDir / pathFromUtf8("ugc_" + mod.identity.id + ".mod");
        fs::path registered = mod.descriptorPath;
        if (registered.empty() || !isRegularFile(registered) ||
            !samePath(registered.parent_path(), modDir))
            registered = ugcFile;
        std::optional<DescriptorDocument> registeredDocument;
        if (isRegularFile(registered)) {
            const Registration reg =
                readRegistration(registered, installation.userDataPath, traits().steamAppId);
            if (reg.document && !reg.declared.empty() && reg.declaredArchive == archive &&
                samePath(reg.declared, mod.contentPath)) {
                refs.push_back(reg.ref);
                continue;
            }
            registeredDocument = reg.document;
        }
        // mod/ugc_<id>.mod is (re)written below; never over another mod's
        // registration (e.g. a local mod that happens to use that name).
        if (isRegularFile(ugcFile)) {
            const Registration owner =
                readRegistration(ugcFile, installation.userDataPath, traits().steamAppId);
            if (owner.workshopId != mod.identity.id)
                return makeError(ErrorCode::ValidationFailed,
                                 "The Workshop mod " + label + " cannot be registered because " +
                                     owner.ref +
                                     " belongs to another mod. Rename that file, then rescan.",
                                 toString(mod.identity) + ": " + pathToUtf8(ugcFile));
        }

        // The content copy's own path= is often stale (the author's machine),
        // so the generated descriptor gets the actual location.
        std::optional<DescriptorDocument> basis;
        if (!archive) {
            if (const std::optional<fs::path> file = contentDescriptorFile(mod.contentPath)) {
                if (auto parsed = readDescriptorDocument(*file); parsed)
                    basis = std::move(parsed).value();
            }
        }
        if (!basis)
            basis = registeredDocument;
        if (!basis) {
            if (mod.descriptor.name.empty())
                return makeError(ErrorCode::NotFound,
                                 "The Workshop mod " + label +
                                     " has no readable descriptor. "
                                     "Verify its files in Steam.",
                                 toString(mod.identity));
            basis = toDocument(mod.descriptor);
        }
        DescriptorLocation location;
        (archive ? location.archive : location.path) = genericAbsolute(mod.contentPath);
        location.remoteFileId = mod.identity.id;
        std::string contents = writeDescriptor(relocateDescriptor(std::move(*basis), location));
        const std::string ref = ugcReference(mod.identity.id);
        refs.push_back(ref);
        if (fileHolds(ugcFile, contents))
            continue;
        plan.configWrites.push_back(ConfigWrite{
            ugcFile, std::move(contents), "Workshop descriptor for " + label + " (" + ref + ")"});
    }

    for (const std::string& ref : refs) {
        if (!isValidUtf8(ref))
            return makeError(ErrorCode::ValidationFailed,
                             "A mod descriptor file name is not valid UTF-8 and cannot be listed "
                             "in dlc_load.json. Rename it, then rescan.",
                             ref);
    }
    const fs::path dlcLoadFile = installation.userDataPath / kDlcLoadFile;
    Result<DlcLoadOutput> dlcLoad = composeDlcLoad(dlcLoadFile, refs);
    if (!dlcLoad)
        return std::move(dlcLoad).error();
    plan.warnings = std::move(dlcLoad->warnings);
    if (!dlcLoad->unchanged)
        plan.configWrites.push_back(
            ConfigWrite{dlcLoadFile, std::move(dlcLoad->contents), "load order (dlc_load.json)"});
    return plan;
}

} // namespace pmm::games
