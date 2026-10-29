#pragma once

// The game's own launcher-settings.json, which the Paradox launcher reads to
// learn the game version, the user-data folder and how to start the game.
// Modern layout: <install>/launcher-settings.json; older installations keep it
// in <install>/launcher/launcher-settings.json.
//
// The file is local data written by the game's installer, but it decides what
// gets executed, so it is validated like any other input: fields must have the
// documented types, arguments are strings only, and the executable must
// resolve to a regular file inside the installation folder.

#include "core/model.hpp"
#include "core/result.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::games {

namespace fs = std::filesystem;

struct LauncherSettings {
    std::string gameId;       // "stellaris"
    std::string version;      // display label, "Cygnus v4.5.2 (9776)"
    std::string rawVersion;   // "v4.5.2"
    std::string distPlatform; // "steam"
    std::string gameDataPath; // "%USER_DOCUMENTS%/Paradox Interactive/Stellaris"
    std::string exePath;      // relative to the file's folder, "./stellaris.exe"
    std::vector<std::string> exeArgs;
    fs::path file; // where it was read from; empty when parsed from text
};

inline constexpr std::size_t kMaxLauncherArguments = 64;
inline constexpr std::size_t kMaxLauncherArgumentBytes = 4096;

// ParseError for invalid JSON, ValidationFailed for wrong field types, NUL
// characters, or too many/too long arguments. Unknown fields are ignored.
Result<LauncherSettings> parseLauncherSettings(std::string_view json);

// The first existing candidate, modern layout first.
std::optional<fs::path> findLauncherSettings(const fs::path& installDir);

// NotFound when neither location has the file.
Result<LauncherSettings> readLauncherSettings(const fs::path& installDir);

// Resolves exePath against the settings file's folder, then the installation
// folder. The result must stay inside installDir (lexically) and be a regular
// file; on Windows it must also have the .exe extension. An app bundle
// ("x.app") resolves to x.app/Contents/MacOS/x. ValidationFailed when the path
// leaves the installation, NotFound when nothing exists there.
Result<fs::path> resolveLauncherExecutable(const LauncherSettings& settings,
                                           const fs::path& installDir, OsPlatform os);

// Maps "%USER_DOCUMENTS%/<relative>" onto `documentsDir`. nullopt for any
// other form, empty base folders, or relative parts containing "..".
std::optional<fs::path> resolveGameDataPath(std::string_view gameDataPath,
                                            const fs::path& documentsDir);

} // namespace pmm::games
