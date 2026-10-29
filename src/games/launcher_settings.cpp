#include "games/launcher_settings.hpp"

#include "platform/fs.hpp"
#include "platform/steam.hpp"

#include <nlohmann/json.hpp>

#include <system_error>
#include <utility>

namespace pmm::games {
namespace {

using platform::pathToUtf8;
using platform::steam::keyEquals;

constexpr std::uint64_t kMaxSettingsBytes = 1024 * 1024;
constexpr std::string_view kUserDocuments = "%USER_DOCUMENTS%";

Error invalidField(std::string_view field, std::string_view problem) {
    return Error(ErrorCode::ValidationFailed,
                 "The game's launcher-settings.json is invalid: \"" + std::string(field) + "\" " +
                     std::string(problem) + ". Verify the game files in Steam.",
                 std::string(field) + ": " + std::string(problem));
}

Status readString(const nlohmann::json& object, const char* field, std::string& out) {
    const auto it = object.find(field);
    if (it == object.end() || it->is_null())
        return {};
    if (!it->is_string())
        return invalidField(field, "must be a string");
    out = it->get<std::string>();
    if (out.find('\0') != std::string::npos)
        return invalidField(field, "contains a NUL character");
    return {};
}

} // namespace

Result<LauncherSettings> parseLauncherSettings(std::string_view text) {
    const nlohmann::json json =
        nlohmann::json::parse(text.data(), text.data() + text.size(), nullptr, false, true);
    if (json.is_discarded())
        return makeError(ErrorCode::ParseError,
                         "The game's launcher-settings.json is not valid JSON. Verify the game "
                         "files in Steam.",
                         "launcher-settings.json: JSON syntax error");
    if (!json.is_object())
        return invalidField("(document)", "must be a JSON object");

    LauncherSettings settings;
    PMM_TRY(readString(json, "gameId", settings.gameId));
    PMM_TRY(readString(json, "version", settings.version));
    PMM_TRY(readString(json, "rawVersion", settings.rawVersion));
    PMM_TRY(readString(json, "distPlatform", settings.distPlatform));
    PMM_TRY(readString(json, "gameDataPath", settings.gameDataPath));
    PMM_TRY(readString(json, "exePath", settings.exePath));

    if (const auto args = json.find("exeArgs"); args != json.end() && !args->is_null()) {
        if (!args->is_array())
            return invalidField("exeArgs", "must be a list of strings");
        if (args->size() > kMaxLauncherArguments)
            return invalidField("exeArgs", "has too many entries");
        for (const nlohmann::json& arg : *args) {
            if (!arg.is_string())
                return invalidField("exeArgs", "must contain only strings");
            std::string value = arg.get<std::string>();
            if (value.find('\0') != std::string::npos)
                return invalidField("exeArgs", "contains a NUL character");
            if (value.size() > kMaxLauncherArgumentBytes)
                return invalidField("exeArgs", "contains an overly long argument");
            settings.exeArgs.push_back(std::move(value));
        }
    }
    return settings;
}

std::optional<fs::path> findLauncherSettings(const fs::path& installDir) {
    for (const fs::path& candidate : {installDir / "launcher-settings.json",
                                      installDir / "launcher" / "launcher-settings.json"}) {
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec))
            return candidate;
    }
    return std::nullopt;
}

Result<LauncherSettings> readLauncherSettings(const fs::path& installDir) {
    const auto file = findLauncherSettings(installDir);
    if (!file)
        return makeError(ErrorCode::NotFound, "The game folder has no launcher-settings.json.",
                         pathToUtf8(installDir));
    Result<std::string> text = platform::readFile(*file, kMaxSettingsBytes);
    if (!text)
        return std::move(text).error();
    Result<LauncherSettings> settings = parseLauncherSettings(*text);
    if (!settings) {
        Error error = std::move(settings).error();
        error.detail = pathToUtf8(*file) + ": " + error.detail;
        return error;
    }
    settings->file = *file;
    return settings;
}

Result<fs::path> resolveLauncherExecutable(const LauncherSettings& settings,
                                           const fs::path& installDir, OsPlatform os) {
    if (settings.exePath.empty())
        return invalidField("exePath", "is missing");
    const fs::path relative = platform::pathFromUtf8(settings.exePath);
    std::vector<fs::path> bases;
    if (!settings.file.empty())
        bases.push_back(settings.file.parent_path());
    bases.push_back(installDir);

    bool outside = false;
    for (const fs::path& base : bases) {
        fs::path candidate = (base / relative).lexically_normal();
        if (!platform::isWithin(candidate, installDir)) {
            outside = true;
            continue;
        }
        std::error_code ec;
        if (fs::is_directory(candidate, ec) && keyEquals(pathToUtf8(candidate.extension()), ".app"))
            candidate = candidate / "Contents" / "MacOS" / candidate.stem();
        if (!fs::is_regular_file(candidate, ec))
            continue;
        if (os == OsPlatform::Windows && !keyEquals(pathToUtf8(candidate.extension()), ".exe"))
            return invalidField("exePath", "does not name a .exe file");
        return candidate;
    }
    if (outside)
        return invalidField("exePath", "points outside the game folder");
    return makeError(ErrorCode::NotFound,
                     "The game executable named in launcher-settings.json does not exist. Verify "
                     "the game files in Steam.",
                     pathToUtf8(installDir) + ": exePath=" + settings.exePath);
}

std::optional<fs::path> resolveGameDataPath(std::string_view gameDataPath,
                                            const fs::path& documentsDir) {
    if (documentsDir.empty() || gameDataPath.substr(0, kUserDocuments.size()) != kUserDocuments)
        return std::nullopt;
    std::string_view rest = gameDataPath.substr(kUserDocuments.size());
    if (!rest.empty() && rest.front() != '/' && rest.front() != '\\')
        return std::nullopt;
    fs::path result = documentsDir;
    while (!rest.empty()) {
        const std::size_t separator = rest.find_first_of("/\\");
        const std::string_view part = rest.substr(0, separator);
        if (part == "..")
            return std::nullopt;
        if (!part.empty() && part != ".") {
            if (part.find(':') != std::string_view::npos)
                return std::nullopt; // drive letters or streams
            result /= platform::pathFromUtf8(part);
        }
        if (separator == std::string_view::npos)
            break;
        rest.remove_prefix(separator + 1);
    }
    return result;
}

} // namespace pmm::games
