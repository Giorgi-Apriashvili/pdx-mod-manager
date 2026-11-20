#include "games/launcher_adapter.hpp"

#include "core/log.hpp"
#include "core/utf8.hpp"
#include "games/launcher_settings.hpp"
#include "games/supported_version.hpp"
#include "games/workshop_content.hpp"
#include "platform/fs.hpp"
#include "platform/steam.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <system_error>
#include <utility>

namespace pmm::games {
namespace {

namespace steam = platform::steam;
using platform::pathFromUtf8;
using platform::pathToUtf8;
using platform::toGenericUtf8;
using steam::keyEquals;

constexpr std::uint64_t kMaxUserDirBytes = 64ull * 1024;

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

LauncherGameAdapter::LauncherGameAdapter(const GameTraits& traits)
    : m_traits(traits), m_catalog(findGame(traits.key)) {
    assert(m_catalog != nullptr);
}

const GameCatalogEntry& LauncherGameAdapter::catalog() const {
    return *m_catalog;
}

Capabilities LauncherGameAdapter::capabilities(const GameInstallation& installation) const {
    Capabilities caps;
    caps.directLaunch = isRegularFile(installation.executablePath);
    caps.workshopAcquisition = installation.distribution == DistributionSource::Steam &&
                               steam::findSteamApiLibrary(installation.installPath).has_value();
    caps.localMods = true;
    return caps;
}

fs::path LauncherGameAdapter::defaultUserDataPath(const DiscoveryEnvironment& env) const {
    if (env.documentsDir.empty())
        return {};
    return env.documentsDir / "Paradox Interactive" / pathFromUtf8(m_traits.userDataFolder);
}

fs::path LauncherGameAdapter::defaultExecutable() const {
    return pathFromUtf8(m_traits.executable);
}

std::vector<std::string>
LauncherGameAdapter::launchArguments(const std::vector<std::string>& arguments) const {
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

Result<GameInstallation> LauncherGameAdapter::describe(const fs::path& installPath,
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
    std::error_code ec;
    const fs::path absolute = fs::absolute(installPath, ec);
    inst.installPath = (ec ? installPath : absolute).lexically_normal();
    // lexically_normal() keeps a trailing separator ("...\Stellaris\"); its
    // parent_path() would be the folder itself, and inspectInstallation()
    // would not see that it lies in <library>/steamapps/common/.
    if (!inst.installPath.has_filename() && inst.installPath.has_relative_path())
        inst.installPath = inst.installPath.parent_path();
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
        Result<fs::path> exe = resolveLauncherExecutable(*settings, inst.installPath);
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
        inst.executablePath = inst.installPath / defaultExecutable();
        if (!isRegularFile(inst.executablePath)) {
            if (mode == Mode::Manual) {
                std::string message = "The selected folder does not contain the " + gameName +
                                      " executable (" + toGenericUtf8(defaultExecutable()) + ").";
                // A subfolder chosen by mistake, e.g. CK3's binaries/ or launcher/.
                const fs::path parent = inst.installPath.parent_path();
                if (inst.installPath.has_relative_path() && parent != inst.installPath &&
                    isRegularFile(parent / defaultExecutable()))
                    message += " Choose the folder above it, \"" +
                               sanitizeUtf8(pathToUtf8(parent.filename())) + "\", instead.";
                return makeError(ErrorCode::NotFound, std::move(message),
                                 pathToUtf8(inst.executablePath));
            }
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
        std::optional<fs::path> fromSettings;
        if (!fromUserDir && settings && !settings->gameDataPath.empty()) {
            fromSettings = resolveGameDataPath(settings->gameDataPath, env.documentsDir);
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

std::vector<GameInstallation> LauncherGameAdapter::discover(const DiscoveryEnvironment& env) const {
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
LauncherGameAdapter::inspectInstallation(const fs::path& installPath, const fs::path& userDataPath,
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

Result<LaunchPlan>
LauncherGameAdapter::beginLaunchPlan(const GameInstallation& installation) const {
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

    LaunchPlan plan;
    plan.game = std::string(m_traits.key);
    plan.executable = installation.executablePath;
    // The executable's folder, as the Paradox launcher does: the installation
    // folder for Stellaris and EU4, <install>/binaries for CK3 (where its
    // steam_appid.txt lies).
    plan.workingDirectory = installation.executablePath.has_parent_path()
                                ? installation.executablePath.parent_path()
                                : installation.installPath;
    // Also filtered here: installations stored before a flag was dropped.
    plan.arguments = launchArguments(installation.defaultArguments);
    if (installation.distribution == DistributionSource::Steam &&
        !installation.steamAppId.empty()) {
        plan.environment.emplace_back("SteamAppId", installation.steamAppId);
        plan.environment.emplace_back("SteamGameId", installation.steamAppId);
    }
    return plan;
}

ProcessNames LauncherGameAdapter::processNames(const GameInstallation& installation) const {
    ProcessNames names;
    const auto addGame = [&](const std::string& file) {
        if (file.empty())
            return;
        for (const std::string& known : names.game) {
            if (keyEquals(known, file))
                return;
        }
        names.game.push_back(file);
    };
    addGame(pathToUtf8(installation.executablePath.filename()));
    addGame(pathToUtf8(defaultExecutable().filename()));
    for (const std::string_view name : m_traits.extraGameProcesses)
        addGame(std::string(name));
    for (const std::string_view name : m_traits.launcherProcesses)
        names.conflictingWriters.emplace_back(name);
    return names;
}

LoadOrderInfo LauncherGameAdapter::loadOrderInfo() const {
    LoadOrderInfo info;
    info.summary = std::string(m_traits.loadOrderSummary);
    info.verifiedInGame = m_traits.loadOrderVerifiedInGame;
    info.evidence = std::string(m_traits.loadOrderEvidence);
    info.userNote = std::string(m_traits.loadOrderUserNote);
    return info;
}

std::vector<std::string>
LauncherGameAdapter::compatibilityWarnings(const GameInstallation& installation,
                                           const InstalledMod& mod) const {
    std::vector<std::string> warnings;
    const std::string& pattern = mod.descriptor.supportedVersion;
    const std::string gameName(catalog().displayName);
    const std::string key(m_traits.supportedVersionKey);
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
        warnings.push_back("The " + key + " \"" + pattern + "\" is not a version pattern.");
        break;
    }
    if (m_traits.rejectsEarlyWildcards && check.wildcardBeforePatch)
        warnings.push_back(gameName + " reports " + key + " \"" + pattern +
                           "\" as invalid (wildcards are only accepted in the patch number).");
    return warnings;
}

} // namespace pmm::games
