// Hidden tests ("[.real]") that run the EU4 and Stellaris adapters against the
// REAL installations of the machine they run on and print a redacted summary.
// They are strictly read-only: discovery, scanning and reading dlc_load.json
// only read files, and buildLaunchPlan() only computes the writes it would
// make (nothing is applied, nothing is started). The replay step copies the
// registered descriptors into a temporary folder and works on the copy. Run
// explicitly:
//   pmm_games_tests "[.real]" -s
// Results of a run are recorded in docs/evidence/real-scan-2026-10-29.md.

#include "core/log.hpp"
#include "games/descriptor.hpp"
#include "games/eu4.hpp"
#include "games/stellaris.hpp"
#include "platform/fs.hpp"
#include "platform/steam.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

using namespace pmm;
using namespace pmm::games;

namespace {

namespace steam = platform::steam;

std::string shown(const fs::path& path) {
    return redact(platform::toGenericUtf8(path));
}

std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items)
        out += (out.empty() ? "" : " ") + item;
    return out;
}

void printSteamState(const DlcLoadJsonAdapter& adapter, const GameInstallation& inst) {
    const std::string appId(adapter.traits().steamAppId);
    const fs::path library = inst.steamLibraryPath;
    if (auto app = steam::readAppManifest(
            library / "steamapps" / platform::pathFromUtf8("appmanifest_" + appId + ".acf"))) {
        std::cout << "app manifest:   StateFlags " << app->stateFlags << " (fully installed "
                  << (app->fullyInstalled() ? "yes" : "no") << ", update required "
                  << (app->updateRequired() ? "yes" : "no") << "), build " << app->buildId << "\n";
    }
    if (auto workshop =
            steam::readAppWorkshopManifest(steam::appWorkshopManifestPath(library, appId))) {
        int installed = 0;
        int pending = 0;
        for (const steam::WorkshopItemState& item : workshop->items) {
            installed += item.installed ? 1 : 0;
            pending += item.installed && item.hasPendingUpdate() ? 1 : 0;
        }
        std::cout << "workshop acf:   NeedsDownload " << workshop->needsDownload << ", NeedsUpdate "
                  << workshop->needsUpdate << ", items " << workshop->items.size() << " ("
                  << installed << " installed, " << pending << " with a pending update)\n";
    }
    std::error_code ec;
    const fs::path userDir = inst.installPath / "userdir.txt";
    if (!fs::is_regular_file(userDir, ec)) {
        std::cout << "userdir.txt:    absent\n";
    } else if (auto read = readUserDirFile(userDir); !read) {
        std::cout << "userdir.txt:    ignored (" << read.error().message << ")\n";
    } else {
        std::cout << "userdir.txt:    " << (*read ? shown(**read) : std::string("empty")) << "\n";
    }
    std::cout << "steam_appid.txt: "
              << (fs::is_regular_file(inst.installPath / "steam_appid.txt", ec) ? "present"
                                                                                : "absent")
              << "\n";
}

void runRealScan(const DlcLoadJsonAdapter& adapter) {
    setLogSink({}); // keep the summary readable
    const DiscoveryEnvironment env = DiscoveryEnvironment::current();
    const std::vector<GameInstallation> found = adapter.discover(env);
    std::cout << "== " << adapter.catalog().displayName << "\n"
              << "discovered installations: " << found.size() << "\n";
    REQUIRE_FALSE(found.empty());
    const GameInstallation& inst = found.front();
    std::cout << "install:        " << shown(inst.installPath) << "\n"
              << "user data:      " << shown(inst.userDataPath) << "\n"
              << "executable:     " << shown(inst.executablePath) << "\n"
              << "arguments:      " << inst.defaultArguments.size() << " ["
              << joined(inst.defaultArguments) << "]\n"
              << "version:        " << inst.version << " (" << inst.versionLabel << ")\n"
              << "distribution:   " << toString(inst.distribution) << " app " << inst.steamAppId
              << "\n"
              << "steam library:  " << shown(inst.steamLibraryPath) << "\n";
    printSteamState(adapter, inst);
    const Capabilities caps = adapter.capabilities(inst);
    std::cout << "capabilities:   directLaunch " << caps.directLaunch << ", workshopAcquisition "
              << caps.workshopAcquisition << ", localMods " << caps.localMods << "\n";
    for (const std::string& arg : inst.defaultArguments)
        CHECK_FALSE(steam::keyEquals(arg, "-enabletelemetry"));

    auto scanned = adapter.scanMods(inst);
    REQUIRE(scanned.ok());
    const std::vector<InstalledMod>& mods = *scanned;
    std::map<std::string, int> bySource;
    std::map<std::string, int> byState;
    std::map<std::string, int> warningsByPattern;
    int unregistered = 0;
    int withProblems = 0;
    int withWarnings = 0;
    int localWithRemoteId = 0;
    for (const InstalledMod& mod : mods) {
        ++bySource[mod.identity.source];
        ++byState[toString(mod.state)];
        unregistered += mod.descriptorPath.empty() ? 1 : 0;
        withProblems += mod.problems.empty() ? 0 : 1;
        localWithRemoteId +=
            mod.identity.source == source::Local && !mod.descriptor.remoteFileId.empty() ? 1 : 0;
        const std::vector<std::string> warnings = adapter.compatibilityWarnings(inst, mod);
        withWarnings += warnings.empty() ? 0 : 1;
        if (!warnings.empty())
            ++warningsByPattern["\"" + mod.descriptor.supportedVersion + "\" -> " +
                                std::to_string(warnings.size()) + " warning(s)"];
        // Every registered ugc_<id>.mod is the Workshop item <id>.
        const std::string file = platform::pathToUtf8(mod.descriptorPath.filename());
        if (file.rfind("ugc_", 0) == 0 && mod.identity.source == source::SteamWorkshop)
            CHECK(file == "ugc_" + mod.identity.id + ".mod");
    }
    std::cout << "mods:           " << mods.size() << "\n";
    for (const auto& [sourceKey, count] : bySource)
        std::cout << "  source " << sourceKey << ": " << count << "\n";
    for (const auto& [state, count] : byState)
        std::cout << "  state " << state << ": " << count << "\n";
    std::cout << "  local mods carrying a remote_file_id: " << localWithRemoteId << "\n"
              << "  unregistered Workshop folders: " << unregistered << "\n"
              << "  with scan problems: " << withProblems << "\n"
              << "  with compatibility warnings: " << withWarnings << "\n";
    for (const auto& [pattern, count] : warningsByPattern)
        std::cout << "    supported_version " << pattern << ": " << count << " mod(s)\n";
    for (const InstalledMod& mod : mods) {
        for (const std::string& problem : mod.problems)
            std::cout << "  problem " << toString(mod.identity) << ": " << redact(problem) << "\n";
    }

    auto current = adapter.readCurrentConfiguration(inst, mods);
    REQUIRE(current.ok());
    std::cout << "dlc_load.json enabled mods: " << current->size() << "\n";
    std::vector<InstalledMod> ordered;
    int unresolved = 0;
    for (std::size_t i = 0; i < current->size(); ++i) {
        const PlaysetEntry& entry = (*current)[i];
        bool resolved = false;
        for (const InstalledMod& mod : mods) {
            if (mod.identity == entry.identity) {
                ordered.push_back(mod);
                resolved = true;
            }
        }
        unresolved += resolved ? 0 : 1;
        std::cout << "  #" << i + 1 << " " << toString(entry.identity) << " \"" << entry.nameHint
                  << "\"" << (resolved ? "" : " (not installed)") << "\n";
    }
    std::cout << "  unresolved: " << unresolved << "\n";

    // The plan for the current configuration (computed, never applied): zero
    // writes means this code reproduces the official launcher's files byte for
    // byte. Every write must target the user-data folder and change the file.
    auto plan = adapter.buildLaunchPlan(inst, ordered);
    REQUIRE(plan.ok());
    std::cout << "launch plan for the current configuration: " << plan->configWrites.size()
              << " write(s)\n"
              << "  executable:  " << shown(plan->executable) << "\n"
              << "  working dir: " << shown(plan->workingDirectory) << "\n"
              << "  arguments:   [" << joined(plan->arguments) << "]\n";
    for (const auto& [name, value] : plan->environment)
        std::cout << "  environment: " << name << "=" << value << "\n";
    for (const std::string& warning : plan->warnings)
        std::cout << "  warning: " << redact(warning) << "\n";
    for (const ConfigWrite& write : plan->configWrites) {
        auto existing = platform::readFile(write.target);
        std::cout << "  would write " << shown(write.target) << " (" << write.purpose << "): "
                  << (existing ? "replaces " + std::to_string(existing->size()) + " bytes"
                               : std::string("new file"))
                  << " with " << write.contents.size() << " bytes\n";
        CHECK(platform::isWithin(write.target, inst.userDataPath));
        CHECK_FALSE((existing && *existing == write.contents));
    }
    for (const std::string& arg : plan->arguments)
        CHECK_FALSE(steam::keyEquals(arg, "-enabletelemetry"));

    // Regenerating each launcher-written Workshop registration from its
    // content descriptor must give the same bytes.
    int compared = 0;
    int identical = 0;
    for (const InstalledMod& mod : mods) {
        std::error_code ec;
        if (mod.identity.source != source::SteamWorkshop || mod.descriptorPath.empty() ||
            !fs::is_directory(mod.contentPath, ec))
            continue;
        auto content = readDescriptorDocument(mod.contentPath / "descriptor.mod");
        auto registered = platform::readFile(mod.descriptorPath);
        if (!content || !registered)
            continue;
        ++compared;
        const std::string generated = writeDescriptor(relocateDescriptor(
            *content, {platform::toGenericUtf8(mod.contentPath), "", mod.identity.id}));
        identical += generated == *registered ? 1 : 0;
        if (generated != *registered)
            std::cout << "  regenerated descriptor differs: " << toString(mod.identity) << "\n";
    }
    std::cout << "regenerated Workshop descriptors identical to the launcher's: " << identical
              << " of " << compared << "\n";
}

// The state right after a reinstall, replayed with the real descriptors: the
// registered .mod files and dlc_load.json are copied into a temporary folder
// with the Steam library replaced by an empty one. Only the copy is written.
void replayWithoutWorkshopContent(const DlcLoadJsonAdapter& adapter) {
    setLogSink({});
    const std::vector<GameInstallation> found = adapter.discover(DiscoveryEnvironment::current());
    REQUIRE_FALSE(found.empty());
    const GameInstallation& inst = found.front();
    test::TempDir temp("pmm real replay");
    const fs::path library = temp.path() / "Steam";
    fs::create_directories(library / "steamapps");
    GameInstallation replay = inst;
    replay.userDataPath = temp.path() / "user data";
    replay.steamLibraryPath = library;
    const std::string realLibrary = platform::toGenericUtf8(inst.steamLibraryPath);
    const std::string emptyLibrary = platform::toGenericUtf8(library);
    for (const auto& entry : fs::directory_iterator(inst.userDataPath / "mod")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".mod")
            continue;
        auto text = platform::readFile(entry.path());
        REQUIRE(text.ok());
        test::writeText(replay.userDataPath / "mod" / entry.path().filename(),
                        test::replaceAll(*text, realLibrary + "/", emptyLibrary + "/"));
    }
    test::writeText(replay.userDataPath / "dlc_load.json",
                    test::readText(inst.userDataPath / "dlc_load.json"));

    auto mods = adapter.scanMods(replay);
    REQUIRE(mods.ok());
    std::map<std::string, int> inventory;
    for (const InstalledMod& mod : *mods)
        ++inventory[mod.identity.source + " " + toString(mod.state)];
    std::cout << "== " << adapter.catalog().displayName
              << ": replay without Workshop content\n  inventory: " << mods->size() << "\n";
    for (const auto& [key, count] : inventory)
        std::cout << "    " << key << ": " << count << "\n";
    auto current = adapter.readCurrentConfiguration(replay, *mods);
    REQUIRE(current.ok());
    std::map<std::string, int> entries;
    for (const PlaysetEntry& entry : *current) {
        bool installed = false;
        for (const InstalledMod& mod : *mods)
            installed = installed || mod.identity == entry.identity;
        ++entries[entry.identity.source + (installed ? " installed" : " not installed")];
        if (entry.identity.source == source::SteamWorkshop)
            CHECK_FALSE(installed);
    }
    std::cout << "  dlc_load.json entries: " << current->size() << "\n";
    for (const auto& [key, count] : entries)
        std::cout << "    " << key << ": " << count << "\n";
    for (const InstalledMod& mod : *mods)
        CHECK(mod.identity.source == source::Local);
}

} // namespace

TEST_CASE("Real Stellaris installation (read-only)", "[.real]") {
    runRealScan(StellarisAdapter());
    replayWithoutWorkshopContent(StellarisAdapter());
}

TEST_CASE("Real EU4 installation (read-only)", "[.real]") {
    runRealScan(Eu4Adapter());
    replayWithoutWorkshopContent(Eu4Adapter());
}
