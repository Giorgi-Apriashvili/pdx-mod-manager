#include "games/workshop_content.hpp"

#include "core/log.hpp"
#include "core/utf8.hpp"
#include "platform/fs.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <system_error>
#include <utility>

namespace pmm::games {

namespace steam = platform::steam;
using platform::pathToUtf8;
using platform::toGenericUtf8;

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

bool isWorkshopId(std::string_view id) noexcept {
    if (id.empty() || id.size() > 20 || id.front() == '0')
        return false;
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(id.data(), id.data() + id.size(), value);
    return error == std::errc() && end == id.data() + id.size();
}

bool hasExtension(const fs::path& path, std::string_view extension) {
    return steam::keyEquals(pathToUtf8(path.extension()), extension);
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

std::string genericAbsolute(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    return toGenericUtf8((ec ? path : absolute).lexically_normal());
}

const steam::WorkshopItemState* WorkshopContent::findItem(std::string_view id) const {
    for (const auto& manifest : manifests) {
        if (const auto* item = manifest.findItem(id))
            return item;
    }
    return nullptr;
}

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

void applyWorkshopState(InstalledMod& mod, const WorkshopContent& content) {
    const steam::WorkshopItemState* item = content.findItem(mod.identity.id);
    if (item != nullptr && !item->installed && !item->subscribedBy.empty())
        mod.state = InstallState::Downloading;
    else if (item != nullptr && item->installed && item->hasPendingUpdate() &&
             mod.state == InstallState::Ready)
        mod.state = InstallState::NeedsUpdate;
    // Only Steam's record of the installed version can be compared with the
    // Workshop: a folder's modification time is the local download time.
    mod.contentUpdatedAt = item != nullptr && item->timeUpdated > 0
                               ? item->timeUpdated
                               : platform::lastWriteUnixSeconds(mod.contentPath);
    mod.contentTimeFromSteam = item != nullptr && item->installed && item->timeUpdated > 0;
    mod.workshopManifest.clear();
    mod.latestManifest.clear();
    mod.latestUpdatedAt = 0;
    if (item != nullptr && item->installed) {
        if (steam::WorkshopItemState::isKnownManifest(item->manifest))
            mod.workshopManifest = item->manifest;
        if (steam::WorkshopItemState::isKnownManifest(item->latestManifest))
            mod.latestManifest = item->latestManifest;
        mod.latestUpdatedAt = std::max<std::int64_t>(0, item->latestTimeUpdated);
    }
}

void dropUnstorablePaths(InstalledMod& mod) {
    const auto drop = [&](fs::path& path, std::string_view what) {
        if (path.empty() || platform::isUtf8Representable(path))
            return;
        markBroken(mod, std::string(what) + " " + sanitizeUtf8(toGenericUtf8(path)) +
                            " has a name with an invalid character, so it cannot be saved. "
                            "Rename it, then rescan.");
        path.clear();
    };
    drop(mod.contentPath, "The mod content");
    drop(mod.descriptorPath, "The descriptor file");
}

namespace {

// One Broken entry for the mods of `group` (see mergeSharedIdentities), in
// the order of their names.
InstalledMod sharedIdentityMod(std::span<const InstalledMod> group,
                               const SharedIdentityNames& names) {
    InstalledMod merged;
    merged.identity = group.front().identity;
    // A reference to any of them in the game's configuration resolves to
    // this entry, by this reference or by the identity of what it names.
    merged.descriptorRef = group.front().descriptorRef;
    std::string files;
    bool replaced = false;
    for (std::size_t i = 0; i < group.size(); ++i) {
        const InstalledMod& mod = group[i];
        files += i == 0 ? "" : i + 1 == group.size() ? " and " : ", ";
        files += mod.descriptorRef;
        if (!mod.descriptor.name.empty())
            files += " (\"" + mod.descriptor.name + "\")";
        replaced = replaced || !platform::isUtf8Representable((mod.*names.named).filename());
    }
    markBroken(merged, "The " + std::string(names.plural) + " " + files +
                           " all give this mod the id \"" + merged.identity.id +
                           "\", so they cannot be told apart. Rename them so that each has a "
                           "distinct name, then rescan." +
                           (replaced ? " Characters in file names that are not valid Unicode "
                                       "are shown as \xEF\xBF\xBD."
                                     : ""));
    return merged;
}

} // namespace

std::vector<InstalledMod> mergeSharedIdentities(std::vector<InstalledMod> mods,
                                                const SharedIdentityNames& names,
                                                std::string_view logComponent) {
    // The native name orders files that read the same as UTF-8.
    std::sort(mods.begin(), mods.end(), [&](const InstalledMod& a, const InstalledMod& b) {
        if (a.identity != b.identity)
            return a.identity < b.identity;
        return (a.*names.named).native() < (b.*names.named).native();
    });
    std::vector<InstalledMod> out;
    out.reserve(mods.size());
    for (std::size_t first = 0; first < mods.size();) {
        std::size_t end = first + 1;
        while (end < mods.size() && mods[end].identity == mods[first].identity)
            ++end;
        if (end - first == 1) {
            out.push_back(std::move(mods[first]));
        } else {
            out.push_back(sharedIdentityMod(std::span(mods).subspan(first, end - first), names));
            logWarning(
                logComponent, "Several " + std::string(names.plural) + " give a mod the same id",
                {{"mod", toString(out.back().identity)}, {"files", std::to_string(end - first)}});
        }
        first = end;
    }
    return out;
}

std::string steamStateNote(const steam::WorkshopItemState* item) {
    if (item == nullptr)
        return "not in Steam's Workshop manifest";
    if (item->installed)
        return "listed as installed, but the folder is missing";
    return item->subscribedBy.empty() ? "not subscribed" : "subscribed, download pending";
}

std::optional<Status> checkWorkshopContentPath(const fs::path& installPath) {
    const std::string shown = pathToUtf8(installPath);
    std::error_code ec;
    const auto status = fs::status(installPath, ec);
    if (ec || !fs::exists(status))
        return makeError(ErrorCode::NotFound, "the content folder reported by Steam does not exist",
                         shown);
    if (fs::is_regular_file(status)) {
        const auto size = fs::file_size(installPath, ec);
        if (ec || size == 0)
            return makeError(ErrorCode::ValidationFailed, "the downloaded file is empty", shown);
        return Status{};
    }
    if (!fs::is_directory(status))
        return makeError(ErrorCode::ValidationFailed,
                         "the content path is neither a folder nor a file", shown);
    return std::nullopt;
}

Status requireNonEmptyFile(const fs::path& file, std::string_view shown, const fs::path& folder) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec || size == 0)
        return makeError(ErrorCode::ValidationFailed, std::string(shown) + " is empty",
                         pathToUtf8(folder));
    return {};
}

} // namespace pmm::games
