// Hidden test ("[.real]") that runs the Stellaris adapter against the REAL
// installation of the machine it runs on and prints a redacted summary. It is
// strictly read-only: discovery, scanning and reading dlc_load.json only read
// files, and buildLaunchPlan() only computes the writes it would make (nothing
// is applied). Run explicitly:
//   pmm_games_tests "[.real]" -s

#include "core/log.hpp"
#include "games/descriptor.hpp"
#include "games/stellaris.hpp"
#include "platform/fs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

using namespace pmm;
using namespace pmm::games;

namespace {

std::string shown(const fs::path& path) {
    return redact(platform::toGenericUtf8(path));
}

} // namespace

TEST_CASE("Real Stellaris installation (read-only)", "[.real]") {
    setLogSink({}); // keep the summary readable
    const StellarisAdapter adapter;
    const DiscoveryEnvironment env = DiscoveryEnvironment::current();
    const std::vector<GameInstallation> found = adapter.discover(env);
    std::cout << "discovered installations: " << found.size() << "\n";
    REQUIRE_FALSE(found.empty());
    const GameInstallation& inst = found.front();
    std::cout << "install:        " << shown(inst.installPath) << "\n"
              << "user data:      " << shown(inst.userDataPath) << "\n"
              << "executable:     " << shown(inst.executablePath) << "\n"
              << "arguments:      " << inst.defaultArguments.size() << " ["
              << (inst.defaultArguments.empty() ? "" : inst.defaultArguments.front()) << "]\n"
              << "version:        " << inst.version << " (" << inst.versionLabel << ")\n"
              << "distribution:   " << toString(inst.distribution) << " app " << inst.steamAppId
              << "\n"
              << "steam library:  " << shown(inst.steamLibraryPath) << "\n";

    auto scanned = adapter.scanMods(inst);
    REQUIRE(scanned.ok());
    const std::vector<InstalledMod>& mods = *scanned;
    std::map<std::string, int> bySource;
    std::map<std::string, int> byState;
    int unregistered = 0;
    int withProblems = 0;
    int withWarnings = 0;
    for (const InstalledMod& mod : mods) {
        ++bySource[mod.identity.source];
        ++byState[toString(mod.state)];
        unregistered += mod.descriptorPath.empty() ? 1 : 0;
        withProblems += mod.problems.empty() ? 0 : 1;
        withWarnings += adapter.compatibilityWarnings(inst, mod).empty() ? 0 : 1;
    }
    std::cout << "mods:           " << mods.size() << "\n";
    for (const auto& [sourceKey, count] : bySource)
        std::cout << "  source " << sourceKey << ": " << count << "\n";
    for (const auto& [state, count] : byState)
        std::cout << "  state " << state << ": " << count << "\n";
    std::cout << "  unregistered Workshop folders: " << unregistered << "\n"
              << "  with scan problems: " << withProblems << "\n"
              << "  with compatibility warnings: " << withWarnings << "\n";
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
        if (i < 5)
            std::cout << "  #" << i + 1 << " " << toString(entry.identity) << " \""
                      << entry.nameHint << "\"\n";
        bool resolved = false;
        for (const InstalledMod& mod : mods) {
            if (mod.identity == entry.identity) {
                ordered.push_back(mod);
                resolved = true;
            }
        }
        unresolved += resolved ? 0 : 1;
    }
    std::cout << "  unresolved: " << unresolved << "\n";

    // The plan for the current configuration: zero writes means this code
    // reproduces the official launcher's dlc_load.json byte for byte.
    auto plan = adapter.buildLaunchPlan(inst, ordered);
    REQUIRE(plan.ok());
    std::cout << "launch plan for the current configuration: " << plan->configWrites.size()
              << " write(s), " << plan->arguments.size() << " argument(s), "
              << plan->environment.size() << " environment variable(s)\n";
    for (const ConfigWrite& write : plan->configWrites)
        std::cout << "  would write " << shown(write.target) << " (" << write.purpose << ")\n";

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
    }
    std::cout << "regenerated Workshop descriptors identical to the launcher's: " << identical
              << " of " << compared << "\n";
    CHECK(unresolved == 0);
}
