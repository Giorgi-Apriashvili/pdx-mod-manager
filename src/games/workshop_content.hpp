#pragma once

// File-system, Steam library and Workshop helpers shared by the adapter
// families (games/dlc_load_adapter.cpp, games/content_load_adapter.cpp).
// Internal to the games library: included by its sources only.

#include "core/model.hpp"
#include "core/result.hpp"
#include "platform/steam.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::games {

namespace fs = std::filesystem;

bool isDirectory(const fs::path& path);
bool isRegularFile(const fs::path& path);
bool samePath(const fs::path& a, const fs::path& b);
// Canonical decimal uint64, as Steam names Workshop content folders.
bool isWorkshopId(std::string_view id) noexcept;
// `extension` with its dot, compared ASCII case-insensitively.
bool hasExtension(const fs::path& path, std::string_view extension);
// The folders (or regular files) directly inside `directory`, in UTF-8 file
// name order. Empty when the directory cannot be read.
std::vector<fs::path> sortedEntries(const fs::path& directory, bool directories);
// Absolute, lexically normal, generic UTF-8 form.
std::string genericAbsolute(const fs::path& path);

// The Workshop content folders of one game in the given Steam libraries, and
// Steam's Workshop manifests (appworkshop_<app>.acf) of those libraries.
struct WorkshopContent {
    std::map<std::string, fs::path> folders; // id -> folder (first library wins)
    std::vector<platform::steam::AppWorkshopManifest> manifests;

    const platform::steam::WorkshopItemState* findItem(std::string_view id) const;
};

WorkshopContent scanWorkshopContent(const std::vector<fs::path>& libraries, std::string_view appId,
                                    std::string_view logComponent);

// The installation's own Steam library, the libraries its libraryfolders.vdf
// lists, then the `inferred` library roots that exist, each once.
std::vector<fs::path> collectLibraries(const GameInstallation& installation,
                                       const std::vector<fs::path>& inferred,
                                       std::string_view logComponent);

void markBroken(InstalledMod& mod, std::string problem);

// Downloading or NeedsUpdate as Steam's Workshop manifest reports the item
// mod.identity.id, and the time its content (mod.contentPath) was updated.
void applyWorkshopState(InstalledMod& mod, const WorkshopContent& content);

// The inventory is stored as UTF-8 text, so a path without a UTF-8 form (names
// with unpaired surrogates) cannot be saved. Such a mod stays in the inventory
// as Broken, without that path, instead of making the whole inventory fail to
// save.
void dropUnstorablePaths(InstalledMod& mod);

// How mergeSharedIdentities() names the files or folders that give a mod its
// id.
struct SharedIdentityNames {
    std::string_view plural;                  // "descriptor files"
    fs::path InstalledMod::* named = nullptr; // the path whose name is the id
};

// Sorted by identity, each identity once: the mods that share an identity
// (names that differ only in unpaired surrogates, which have no UTF-8 form
// and read as U+FFFD, as does a literal U+FFFD, or only in the case of an
// extension in a case-sensitive folder) become one Broken entry that names
// them all. Playsets and share codes refer to mods by id, so no single file
// may stand for it, and no other id is made up for one of them.
std::vector<InstalledMod> mergeSharedIdentities(std::vector<InstalledMod> mods,
                                                const SharedIdentityNames& names,
                                                std::string_view logComponent);

// How Steam sees an item whose content is not on disk (diagnostics only).
std::string steamStateNote(const platform::steam::WorkshopItemState* item);

// What every game's Workshop content check starts with: the path Steam
// reported exists, and a single file (a legacy zipped item) is not empty.
// nullopt for a folder, whose content the game's own rule decides.
std::optional<Status> checkWorkshopContentPath(const fs::path& installPath);

// ValidationFailed ("<shown> is empty") unless `file` has content; `folder`
// is the content folder the error detail names.
Status requireNonEmptyFile(const fs::path& file, std::string_view shown, const fs::path& folder);

} // namespace pmm::games
