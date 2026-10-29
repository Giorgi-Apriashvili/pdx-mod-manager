#include "games/dlc_load_adapter.hpp"

#include "core/log.hpp"
#include "core/utf8.hpp"
#include "games/descriptor.hpp"
#include "games/launcher_settings.hpp"
#include "games/supported_version.hpp"
#include "platform/fs.hpp"
#include "platform/steam.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <map>
#include <set>
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
constexpr std::uint64_t kMaxUserDirBytes = 64ull * 1024;
constexpr const char* kDlcLoadFile = "dlc_load.json";
constexpr const char* kModFolder = "mod";
constexpr const char* kContentDescriptor = "descriptor.mod";

bool isDirectory(const fs::path& path) {
    std::error_code ec;
    return !path.empty() && fs::is_directory(path, ec);
}

bool isRegularFile(const fs::path& path) {
    std::error_code ec;
    return !path.empty() && fs::is_regular_file(path, ec);
}

bool samePath(const fs::path& a, const fs::path& b) {
    return platform::isWithin(a, b) && platform::isWithin(b, a);
}

// Canonical decimal uint64, as Steam names Workshop content folders.
bool isWorkshopId(std::string_view id) noexcept {
    if (id.empty() || id.size() > 20 || id.front() == '0')
        return false;
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(id.data(), id.data() + id.size(), value);
    return error == std::errc() && end == id.data() + id.size();
}

bool hasExtension(const fs::path& path, std::string_view extension) {
    return keyEquals(pathToUtf8(path.extension()), extension);
}

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

std::string genericAbsolute(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    return toGenericUtf8((ec ? path : absolute).lexically_normal());
}

std::vector<fs::path> sortedEntries(const fs::path& directory, bool directories) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code typeError;
        if (directories ? it->is_directory(typeError) : it->is_regular_file(typeError))
            out.push_back(it->path());
    }
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
        return pathToUtf8(a.filename()) < pathToUtf8(b.filename());
    });
    return out;
}

// First .zip directly inside a legacy Workshop folder.
std::optional<fs::path> legacyArchive(const fs::path& folder) {
    for (const fs::path& file : sortedEntries(folder, false)) {
        if (hasExtension(file, ".zip"))
            return file;
    }
    return std::nullopt;
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

struct WorkshopContent {
    std::map<std::string, fs::path> folders; // id -> folder (first library wins)
    std::vector<steam::AppWorkshopManifest> manifests;

    const steam::WorkshopItemState* findItem(std::string_view id) const {
        for (const auto& manifest : manifests) {
            if (const auto* item = manifest.findItem(id))
                return item;
        }
        return nullptr;
    }
};

WorkshopContent scanWorkshopContent(const std::vector<fs::path>& libraries, std::string_view appId,
                                    std::string_view logComponent) {
    WorkshopContent content;
    for (const fs::path& library : libraries) {
        for (const fs::path& folder :
             sortedEntries(steam::workshopContentDir(library, appId), true)) {
            const std::string id = pathToUtf8(folder.filename());
            if (isWorkshopId(id))
                content.folders.emplace(id, folder);
        }
        const fs::path acf = steam::appWorkshopManifestPath(library, appId);
        if (!isRegularFile(acf))
            continue;
        Result<steam::AppWorkshopManifest> manifest = steam::readAppWorkshopManifest(acf);
        if (manifest)
            content.manifests.push_back(std::move(manifest).value());
        else
            logWarning(logComponent, "Ignoring an unreadable Workshop manifest",
                       {{"file", pathToUtf8(acf)}, {"error", manifest.error().message}});
    }
    return content;
}

std::vector<fs::path> collectLibraries(const GameInstallation& installation,
                                       const std::vector<fs::path>& inferred,
                                       std::string_view logComponent) {
    std::vector<fs::path> libraries;
    const auto add = [&](const fs::path& library) {
        if (library.empty())
            return;
        for (const fs::path& known : libraries) {
            if (samePath(known, library))
                return;
        }
        libraries.push_back(library.lexically_normal());
    };
    const fs::path& own = installation.steamLibraryPath;
    if (!own.empty()) {
        add(own);
        if (isRegularFile(own / "steamapps" / "libraryfolders.vdf")) {
            auto listed = steam::readLibraryFolders(own);
            if (listed) {
                for (const auto& library : *listed)
                    add(library.path);
            } else {
                logWarning(logComponent, "Ignoring an unreadable Steam library list",
                           {{"error", listed.error().message}});
            }
        }
    }
    for (const fs::path& library : inferred) {
        if (isDirectory(library / "steamapps"))
            add(library);
    }
    return libraries;
}

void markBroken(InstalledMod& mod, std::string problem) {
    mod.state = InstallState::Broken;
    mod.problems.push_back(std::move(problem));
}

bool declaredContentExists(const Registration& reg) {
    return reg.declaredArchive ? isRegularFile(reg.declared) : isDirectory(reg.declared);
}

// How Steam sees an item whose content is not on disk (diagnostics only).
std::string steamStateNote(const steam::WorkshopItemState* item) {
    if (item == nullptr)
        return "not in Steam's Workshop manifest";
    if (item->installed)
        return "listed as installed, but the folder is missing";
    return item->subscribedBy.empty() ? "not subscribed" : "subscribed, download pending";
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
        const fs::path descriptorFile = *folder / kContentDescriptor;
        if (reg != nullptr && reg->declaredArchive && isRegularFile(reg->declared) &&
            platform::isWithin(reg->declared, *folder)) {
            contentPath = reg->declared;
        } else if (isRegularFile(descriptorFile)) {
            Result<ModDescriptor> parsed = readDescriptor(descriptorFile);
            if (parsed)
                contentDescriptor = std::move(parsed).value();
            else
                contentProblem = parsed.error().message;
        } else if (auto archive = legacyArchive(*folder)) {
            contentPath = *archive;
            contentProblem = "The legacy (zipped) Workshop item has no descriptor.mod outside "
                             "its archive.";
        } else {
            contentProblem = "The Workshop folder has no descriptor.mod.";
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

    const steam::WorkshopItemState* item = content.findItem(id);
    if (item != nullptr && !item->installed && !item->subscribedBy.empty())
        mod.state = InstallState::Downloading;
    else if (item != nullptr && item->installed && item->hasPendingUpdate() &&
             mod.state == InstallState::Ready)
        mod.state = InstallState::NeedsUpdate;
    mod.contentUpdatedAt = item != nullptr && item->timeUpdated > 0
                               ? item->timeUpdated
                               : platform::lastWriteUnixSeconds(contentPath);
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

std::string_view trimmed(std::string_view text) {
    constexpr std::string_view space = " \t\r\n\f\v";
    const std::size_t first = text.find_first_not_of(space);
    if (first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(space) - first + 1);
}

} // namespace

Result<std::optional<fs::path>> readUserDirFile(const fs::path& file) {
    if (!isRegularFile(file))
        return std::optional<fs::path>();
    Result<std::string> text = platform::readFile(file, kMaxUserDirBytes);
    if (!text)
        return std::move(text).error();
    std::string_view rest = *text;
    if (rest.substr(0, 3) == "\xEF\xBB\xBF")
        rest.remove_prefix(3);
    std::string_view line;
    while (line.empty() && !rest.empty()) {
        const std::size_t end = rest.find('\n');
        line = trimmed(rest.substr(0, end));
        rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
    }
    if (line.size() >= 2 && (line.front() == '"' || line.front() == '\'') &&
        line.back() == line.front())
        line = trimmed(line.substr(1, line.size() - 2));
    if (line.empty())
        return std::optional<fs::path>();

    const auto ignored = [&](std::string_view reason) {
        return makeError(ErrorCode::ValidationFailed,
                         "userdir.txt in the game folder is ignored: " + std::string(reason) + ".",
                         pathToUtf8(file) + ": " + sanitizeUtf8(line));
    };
    const bool control = std::any_of(line.begin(), line.end(), [](char c) {
        return static_cast<unsigned char>(c) < 0x20 || c == '\x7f';
    });
    if (control || !isValidUtf8(line))
        return ignored("it does not contain a valid path");
    fs::path path = pathFromUtf8(line);
    if (!path.is_absolute())
        return ignored("only absolute paths are supported");
    path = path.lexically_normal();
    if (!path.has_filename() && path.has_relative_path())
        path = path.parent_path(); // trailing separator
    return std::optional<fs::path>(std::move(path));
}

std::optional<std::string> workshopIdFromPath(const fs::path& path, std::string_view appId,
                                              fs::path* libraryRoot) {
    std::vector<fs::path> parts;
    for (const fs::path& part : path.lexically_normal())
        parts.push_back(part);
    for (std::size_t i = 0; i + 4 < parts.size(); ++i) {
        if (!keyEquals(pathToUtf8(parts[i]), "steamapps") ||
            !keyEquals(pathToUtf8(parts[i + 1]), "workshop") ||
            !keyEquals(pathToUtf8(parts[i + 2]), "content") || pathToUtf8(parts[i + 3]) != appId)
            continue;
        std::string id = pathToUtf8(parts[i + 4]);
        if (!isWorkshopId(id))
            return std::nullopt;
        if (libraryRoot != nullptr) {
            fs::path root;
            for (std::size_t k = 0; k < i; ++k)
                root /= parts[k];
            *libraryRoot = root;
        }
        return id;
    }
    return std::nullopt;
}

std::string descriptorReference(const fs::path& descriptorFile) {
    return std::string(kModFolder) + "/" + pathToUtf8(descriptorFile.filename());
}

DlcLoadJsonAdapter::DlcLoadJsonAdapter(const GameTraits& traits)
    : m_traits(traits), m_catalog(findGame(traits.key)) {
    assert(m_catalog != nullptr);
}

const GameCatalogEntry& DlcLoadJsonAdapter::catalog() const {
    return *m_catalog;
}

Capabilities DlcLoadJsonAdapter::capabilities(const GameInstallation& installation) const {
    Capabilities caps;
    caps.directLaunch = isRegularFile(installation.executablePath);
    caps.workshopAcquisition =
        installation.distribution == DistributionSource::Steam &&
        steam::findSteamApiLibrary(installation.installPath, installation.platform).has_value();
    caps.localMods = true;
    return caps;
}

fs::path DlcLoadJsonAdapter::defaultUserDataPath(const DiscoveryEnvironment& env) const {
    const fs::path& base = env.platform == OsPlatform::Linux ? env.xdgDataHome : env.documentsDir;
    if (base.empty())
        return {};
    return base / "Paradox Interactive" / pathFromUtf8(m_traits.userDataFolder);
}

fs::path DlcLoadJsonAdapter::defaultExecutable(OsPlatform os) const {
    switch (os) {
    case OsPlatform::Windows:
        return pathFromUtf8(m_traits.windowsExecutable);
    case OsPlatform::MacOS:
        return pathFromUtf8(m_traits.macExecutable);
    case OsPlatform::Linux:
        break;
    }
    return pathFromUtf8(m_traits.linuxExecutable);
}

std::vector<std::string>
DlcLoadJsonAdapter::launchArguments(const std::vector<std::string>& arguments) const {
    std::vector<std::string> out;
    for (const std::string& argument : arguments) {
        const bool dropped =
            std::any_of(m_traits.droppedArguments.begin(), m_traits.droppedArguments.end(),
                        [&](std::string_view flag) { return keyEquals(argument, flag); });
        if (!dropped)
            out.push_back(argument);
    }
    return out;
}

Result<GameInstallation> DlcLoadJsonAdapter::describe(const fs::path& installPath,
                                                      const fs::path& userDataPath,
                                                      const DiscoveryEnvironment& env,
                                                      Mode mode) const {
    const std::string gameName(catalog().displayName);
    if (!isDirectory(installPath))
        return makeError(ErrorCode::NotFound,
                         "The selected " + gameName + " folder does not exist.",
                         pathToUtf8(installPath));

    GameInstallation inst;
    inst.game = std::string(m_traits.key);
    inst.platform = env.platform;
    std::error_code ec;
    const fs::path absolute = fs::absolute(installPath, ec);
    inst.installPath = (ec ? installPath : absolute).lexically_normal();
    inst.manuallyConfigured = mode == Mode::Manual;

    std::optional<LauncherSettings> settings;
    if (auto read = readLauncherSettings(inst.installPath); read) {
        settings = std::move(read).value();
    } else if (read.error().code != ErrorCode::NotFound) {
        if (mode == Mode::Manual)
            return std::move(read).error();
        logWarning(m_traits.logComponent, "Ignoring unusable launcher-settings.json",
                   {{"error", read.error().message}, {"detail", read.error().detail}});
    }
    if (settings && !settings->gameId.empty() &&
        !keyEquals(settings->gameId, m_traits.launcherGameId))
        return makeError(ErrorCode::WrongGame,
                         "The selected folder contains another game (\"" + settings->gameId +
                             "\"), not " + gameName + ".",
                         pathToUtf8(inst.installPath));

    const auto launcherProcess = [&](const fs::path& exe) {
        const ProcessNames names = processNames(inst);
        const std::string file = pathToUtf8(exe.filename());
        return std::any_of(names.conflictingWriters.begin(), names.conflictingWriters.end(),
                           [&](const std::string& n) { return keyEquals(n, file); });
    };
    if (settings) {
        Result<fs::path> exe = resolveLauncherExecutable(*settings, inst.installPath, env.platform);
        if (exe && !launcherProcess(*exe)) {
            inst.executablePath = *exe;
            inst.defaultArguments = launchArguments(settings->exeArgs);
        } else {
            logWarning(
                m_traits.logComponent,
                "launcher-settings.json names no usable game executable; using the default",
                {{"exePath", settings->exePath},
                 {"error", exe ? std::string("names the Paradox launcher") : exe.error().message}});
        }
        inst.version = settings->rawVersion;
        inst.versionLabel = settings->version.empty() ? settings->rawVersion : settings->version;
        if (inst.version.empty() && !settings->version.empty()) {
            if (const auto parts = parseGameVersion(settings->version)) {
                inst.version = "v";
                for (std::size_t i = 0; i < parts->size(); ++i)
                    inst.version += (i == 0 ? "" : ".") + std::to_string((*parts)[i]);
            }
        }
    } else {
        logWarning(m_traits.logComponent,
                   "launcher-settings.json is missing; using the default executable without "
                   "arguments",
                   {{"install", pathToUtf8(inst.installPath)}});
    }
    if (inst.executablePath.empty()) {
        inst.executablePath = inst.installPath / defaultExecutable(env.platform);
        if (!isRegularFile(inst.executablePath)) {
            if (mode == Mode::Manual)
                return makeError(ErrorCode::NotFound,
                                 "The selected folder does not contain the " + gameName +
                                     " executable (" +
                                     toGenericUtf8(defaultExecutable(env.platform)) + ").",
                                 pathToUtf8(inst.executablePath));
            logWarning(m_traits.logComponent, "The game executable was not found",
                       {{"path", pathToUtf8(inst.executablePath)}});
        }
    }

    if (!userDataPath.empty()) {
        if (!isDirectory(userDataPath))
            return makeError(ErrorCode::NotFound,
                             "The selected " + gameName + " user data folder does not exist.",
                             pathToUtf8(userDataPath));
        const fs::path userAbsolute = fs::absolute(userDataPath, ec);
        inst.userDataPath = (ec ? userDataPath : userAbsolute).lexically_normal();
    } else {
        std::optional<fs::path> fromUserDir;
        if (!m_traits.userDirFile.empty()) {
            Result<std::optional<fs::path>> read =
                readUserDirFile(inst.installPath / pathFromUtf8(m_traits.userDirFile));
            if (!read) {
                logWarning(m_traits.logComponent, "Ignoring the game's userdir.txt",
                           {{"error", read.error().message}, {"detail", read.error().detail}});
            } else if (*read) {
                fromUserDir = **read;
                logInfo(m_traits.logComponent, "Using the user data folder from userdir.txt",
                        {{"path", pathToUtf8(*fromUserDir)}});
            }
        }
        const fs::path& base =
            env.platform == OsPlatform::Linux ? env.xdgDataHome : env.documentsDir;
        std::optional<fs::path> fromSettings;
        if (!fromUserDir && settings && !settings->gameDataPath.empty()) {
            fromSettings = resolveGameDataPath(settings->gameDataPath, base);
            if (!fromSettings)
                logWarning(m_traits.logComponent, "Ignoring an unsupported gameDataPath",
                           {{"gameDataPath", settings->gameDataPath}});
        }
        inst.userDataPath = fromUserDir    ? *fromUserDir
                            : fromSettings ? *fromSettings
                                           : defaultUserDataPath(env);
        if (inst.userDataPath.empty())
            return makeError(ErrorCode::NotFound, "The " + gameName +
                                                      " user data folder could not be determined. "
                                                      "Choose it manually.");
    }
    return inst;
}

std::vector<GameInstallation> DlcLoadJsonAdapter::discover(const DiscoveryEnvironment& env) const {
    std::vector<GameInstallation> found;
    if (!env.steamRoot)
        return found;
    auto libraries = steam::readLibraryFolders(*env.steamRoot);
    if (!libraries) {
        logWarning(m_traits.logComponent, "Steam library list is unreadable; using the Steam root",
                   {{"error", libraries.error().message}});
        libraries = std::vector<steam::LibraryFolder>{steam::LibraryFolder{*env.steamRoot, {}, {}}};
    }
    Result<steam::AppManifest> app = steam::findInstalledApp(*libraries, m_traits.steamAppId);
    if (!app) {
        if (app.error().code != ErrorCode::NotFound)
            logWarning(m_traits.logComponent, "Steam app manifest is unreadable",
                       {{"error", app.error().message}});
        return found;
    }
    if (!app->fullyInstalled() || !isDirectory(app->installPath())) {
        logInfo(m_traits.logComponent, "Steam lists the game but it is not fully installed",
                {{"manifest", pathToUtf8(app->manifestPath)}});
        return found;
    }
    Result<GameInstallation> inst = describe(app->installPath(), {}, env, Mode::Discovered);
    if (!inst) {
        logWarning(m_traits.logComponent, "Skipping a Steam installation",
                   {{"error", inst.error().message}});
        return found;
    }
    inst->distribution = DistributionSource::Steam;
    inst->steamAppId = std::string(m_traits.steamAppId);
    inst->steamLibraryPath = app->libraryRoot.lexically_normal();
    found.push_back(std::move(inst).value());
    return found;
}

Result<GameInstallation>
DlcLoadJsonAdapter::inspectInstallation(const fs::path& installPath, const fs::path& userDataPath,
                                        const DiscoveryEnvironment& env) const {
    Result<GameInstallation> inst = describe(installPath, userDataPath, env, Mode::Manual);
    if (!inst)
        return inst;
    // <library>/steamapps/common/<folder> counts as a Steam installation.
    const fs::path& path = inst->installPath;
    const fs::path common = path.parent_path();
    const fs::path steamapps = common.parent_path();
    if (keyEquals(pathToUtf8(common.filename()), "common") &&
        keyEquals(pathToUtf8(steamapps.filename()), "steamapps")) {
        inst->distribution = DistributionSource::Steam;
        inst->steamAppId = std::string(m_traits.steamAppId);
        inst->steamLibraryPath = steamapps.parent_path();
    } else {
        inst->distribution = DistributionSource::Manual;
    }
    return inst;
}

Result<std::vector<InstalledMod>>
DlcLoadJsonAdapter::scanMods(const GameInstallation& installation) const {
    if (installation.userDataPath.empty())
        return makeError(ErrorCode::InvalidArgument,
                         "The " + std::string(catalog().displayName) +
                             " user data folder is not set. Rescan or choose it manually.");
    const GameKey game = std::string(m_traits.key);
    const std::string appId(m_traits.steamAppId);

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
        collectLibraries(installation, inferredLibraries, m_traits.logComponent), appId,
        m_traits.logComponent);

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
            logInfo(m_traits.logComponent,
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
    std::sort(mods.begin(), mods.end(),
              [](const InstalledMod& a, const InstalledMod& b) { return a.identity < b.identity; });
    logInfo(m_traits.logComponent, "Mod scan finished",
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

    const GameKey game = std::string(m_traits.key);
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

    std::set<ModIdentity> seen;
    for (const nlohmann::json& value : *enabled) {
        if (!value.is_string())
            continue;
        std::string ref = sanitizeUtf8(value.get<std::string>());
        std::replace(ref.begin(), ref.end(), '\\', '/');
        PlaysetEntry entry;
        const InstalledMod* mod = findByRef(ref);
        std::string hint = ref;
        std::string versionHint;
        std::optional<ModIdentity> workshop; // a Workshop item that is not installed
        if (mod == nullptr && isPlainModReference(ref)) {
            // Another registration of an inventory mod (e.g. a second .mod for
            // the same Workshop item), or the registration of a Workshop item
            // that is not downloaded: resolve it through its own descriptor.
            const fs::path descriptorFile = installation.userDataPath / pathFromUtf8(ref);
            if (isRegularFile(descriptorFile)) {
                const Registration reg = readRegistration(descriptorFile, installation.userDataPath,
                                                          m_traits.steamAppId);
                const ModIdentity identity = identityOf(reg, game);
                mod = findByIdentity(identity);
                if (identity.source == source::SteamWorkshop)
                    workshop = identity;
                if (reg.document && !reg.descriptor.name.empty())
                    hint = reg.descriptor.name;
                if (reg.document)
                    versionHint = reg.descriptor.version;
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
    const std::string gameName(catalog().displayName);
    if (!isRegularFile(installation.executablePath))
        return makeError(ErrorCode::NotFound,
                         "The " + gameName +
                             " executable was not found. Rescan, or choose the "
                             "game folder again.",
                         pathToUtf8(installation.executablePath));
    if (installation.userDataPath.empty())
        return makeError(ErrorCode::InvalidArgument,
                         "The " + gameName + " user data folder is not set.");
    const fs::path modDir = installation.userDataPath / kModFolder;

    LaunchPlan plan;
    plan.game = std::string(m_traits.key);
    plan.executable = installation.executablePath;
    plan.workingDirectory = installation.installPath;
    // Also filtered here: installations stored before a flag was dropped.
    plan.arguments = launchArguments(installation.defaultArguments);
    if (installation.distribution == DistributionSource::Steam &&
        !installation.steamAppId.empty()) {
        plan.environment.emplace_back("SteamAppId", installation.steamAppId);
        plan.environment.emplace_back("SteamGameId", installation.steamAppId);
    }

    std::vector<std::string> refs;
    std::set<ModIdentity> seen;
    for (const InstalledMod& mod : orderedEnabledMods) {
        const std::string label = modLabel(mod);
        if (mod.identity.game != m_traits.key)
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
                readRegistration(registered, installation.userDataPath, m_traits.steamAppId);
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
                readRegistration(ugcFile, installation.userDataPath, m_traits.steamAppId);
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
            if (auto parsed = readDescriptorDocument(mod.contentPath / kContentDescriptor); parsed)
                basis = std::move(parsed).value();
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

ProcessNames DlcLoadJsonAdapter::processNames(const GameInstallation& installation) const {
    ProcessNames names;
    const auto addGame = [&](const fs::path& exe) {
        const std::string file = pathToUtf8(exe.filename());
        if (file.empty())
            return;
        for (const std::string& known : names.game) {
            if (keyEquals(known, file))
                return;
        }
        names.game.push_back(file);
    };
    addGame(installation.executablePath);
    addGame(defaultExecutable(installation.platform));
    std::span<const std::string_view> launchers = m_traits.linuxLauncherProcesses;
    if (installation.platform == OsPlatform::Windows)
        launchers = m_traits.windowsLauncherProcesses;
    else if (installation.platform == OsPlatform::MacOS)
        launchers = m_traits.macLauncherProcesses;
    for (const std::string_view name : launchers)
        names.conflictingWriters.emplace_back(name);
    return names;
}

LoadOrderInfo DlcLoadJsonAdapter::loadOrderInfo() const {
    LoadOrderInfo info;
    info.summary = std::string(m_traits.loadOrderSummary);
    info.verifiedInGame = false; // pending the probe-mod run, see docs/load-order.md
    info.evidence = std::string(m_traits.loadOrderEvidence);
    return info;
}

std::vector<std::string>
DlcLoadJsonAdapter::compatibilityWarnings(const GameInstallation& installation,
                                          const InstalledMod& mod) const {
    std::vector<std::string> warnings;
    const std::string& pattern = mod.descriptor.supportedVersion;
    const std::string gameName(catalog().displayName);
    const std::string& installed =
        installation.version.empty() ? installation.versionLabel : installation.version;
    const SupportedVersionCheck check = checkSupportedVersion(pattern, installed);
    switch (check.match) {
    case VersionMatch::NotDeclared:
    case VersionMatch::Matches:
    case VersionMatch::UnknownGameVersion:
        break;
    case VersionMatch::Mismatch:
        warnings.push_back("Made for " + gameName + " " + pattern + "; the installed version is " +
                           installed + ".");
        break;
    case VersionMatch::Unparsable:
        warnings.push_back("The supported_version \"" + pattern + "\" is not a version pattern.");
        break;
    }
    if (m_traits.rejectsEarlyWildcards && check.wildcardBeforePatch)
        warnings.push_back(gameName + " reports supported_version \"" + pattern +
                           "\" as invalid (wildcards are only accepted in the patch number).");
    return warnings;
}

} // namespace pmm::games
