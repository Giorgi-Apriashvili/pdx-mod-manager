#pragma once

// Fixtures for the application-layer tests: temporary profiles whose paths
// contain spaces and non-ASCII characters, a folder-scanning game adapter
// double for "stellaris" (installation, Workshop and local mods all live in a
// temporary folder), and helpers that spin the Qt event loop until a condition
// holds. Nothing here touches a real game, Steam folder or Steam client.

#include "app/core/app_runtime.hpp"
#include "core/catalog.hpp"
#include "core/model.hpp"
#include "games/game_adapter.hpp"
#include "platform/fs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace pmm::app::test {

namespace fs = std::filesystem;

inline constexpr const char* kGame = "stellaris";
inline constexpr const char* kAppId = "281990";
inline constexpr const char* kExeName = "fakegame.exe";

inline fs::path u8(std::string_view utf8) {
    return platform::pathFromUtf8(utf8);
}

template <class T> T require(Result<T> result) {
    if (!result.ok())
        FAIL("unexpected error: " << result.error().message << " | " << result.error().detail);
    return std::move(result).value();
}

inline void requireOk(const Status& status) {
    if (!status.ok())
        FAIL("unexpected error: " << status.error().message << " | " << status.error().detail);
}

inline void writeText(const fs::path& path, std::string_view text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    REQUIRE(platform::writeFileAtomic(path, text).ok());
}

// Unique folder "<temp>/pmm app Ünïcødé <stamp>-<n>", removed on destruction.
class TempDir {
public:
    TempDir() {
        static std::atomic<unsigned> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = fs::temp_directory_path() /
                 u8("pmm app Ünïcødé " + std::to_string(stamp) + "-" + std::to_string(counter++));
        fs::create_directories(m_path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const noexcept { return m_path; }

private:
    fs::path m_path;
};

// Processes events (of this thread) until `condition` holds or the timeout
// expires. Returns the final value of the condition.
inline bool waitUntil(const std::function<bool()>& condition,
                      std::chrono::milliseconds timeout = std::chrono::seconds(20)) {
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeout.count())
            return condition();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return true;
}

// A fake "stellaris" world in a temporary folder:
//   <root>/library/steamapps/common/Stellaris/fakegame.exe   installation
//   <root>/library/steamapps/workshop/content/281990/<id>/   Workshop mods
//   <root>/user/mod/<name>/                                  local mods
// A mod folder may contain "name.txt" with its display name.
struct FakeWorld {
    explicit FakeWorld(const fs::path& root)
        : library(root / "library"), install(library / "steamapps" / "common" / "Stellaris"),
          workshop(library / "steamapps" / "workshop" / "content" / kAppId),
          userData(root / u8("user data Ünïcødé")) {
        writeText(install / kExeName, "fake executable");
        fs::create_directories(workshop);
        fs::create_directories(userData / "mod");
    }

    void addWorkshopMod(const std::string& id, const std::string& name) const {
        writeText(workshop / id / "name.txt", name);
    }
    void addLocalMod(const std::string& folder, const std::string& name) const {
        writeText(userData / "mod" / u8(folder) / "name.txt", name);
    }

    fs::path library;
    fs::path install;
    fs::path workshop;
    fs::path userData;
};

// Game adapter double for "stellaris" scanning a FakeWorld.
class FakeAdapter : public games::GameAdapter {
public:
    explicit FakeAdapter(const FakeWorld& world) : m_world(world) {}

    std::vector<PlaysetEntry> currentConfiguration; // returned by readCurrentConfiguration

    const GameCatalogEntry& catalog() const override { return *findGame(kGame); }

    Capabilities capabilities(const GameInstallation&) const override {
        return Capabilities{true, true, true};
    }

    std::vector<GameInstallation> discover(const games::DiscoveryEnvironment& env) const override {
        auto installation = inspectInstallation(m_world.install, m_world.userData, env);
        if (!installation)
            return {};
        installation.value().distribution = DistributionSource::Steam;
        installation.value().steamLibraryPath = m_world.library;
        return {installation.value()};
    }

    Result<GameInstallation>
    inspectInstallation(const fs::path& installPath, const fs::path& userDataPath,
                        const games::DiscoveryEnvironment&) const override {
        std::error_code ec;
        if (!fs::is_regular_file(installPath / kExeName, ec))
            return makeError(ErrorCode::ValidationFailed,
                             "This folder does not contain the game executable.");
        GameInstallation g;
        g.game = kGame;
        g.installPath = installPath;
        g.userDataPath = userDataPath.empty() ? m_world.userData : userDataPath;
        g.executablePath = installPath / kExeName;
        g.distribution = DistributionSource::Manual;
        g.steamAppId = kAppId;
        g.version = "v4.5.2";
        g.versionLabel = "Fake v4.5.2";
        return g;
    }

    Result<std::vector<InstalledMod>>
    scanMods(const GameInstallation& installation) const override {
        std::vector<InstalledMod> mods;
        const auto scan = [&](const fs::path& folder, const char* sourceKey) {
            std::error_code ec;
            for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_directory())
                    continue;
                InstalledMod mod;
                const std::string id = platform::pathToUtf8(it->path().filename());
                mod.identity = ModIdentity{kGame, sourceKey, id};
                mod.contentPath = it->path();
                auto name = platform::readFile(it->path() / "name.txt");
                mod.descriptor.name = name ? name.value() : id;
                mod.descriptor.version = "1.0";
                mod.descriptor.supportedVersion = "v4.5.*";
                mod.descriptorRef = "mod/" + id + ".mod";
                mod.contentUpdatedAt = platform::lastWriteUnixSeconds(it->path());
                mods.push_back(std::move(mod));
            }
        };
        scan(m_world.workshop, source::SteamWorkshop);
        scan(installation.userDataPath / "mod", source::Local);
        return mods;
    }

    Result<std::vector<PlaysetEntry>>
    readCurrentConfiguration(const GameInstallation&,
                             std::span<const InstalledMod>) const override {
        return currentConfiguration;
    }

    Result<LaunchPlan> buildLaunchPlan(const GameInstallation& installation,
                                       std::span<const InstalledMod> mods) const override {
        LaunchPlan plan;
        plan.game = kGame;
        plan.executable = installation.executablePath;
        plan.workingDirectory = installation.installPath;
        std::string order;
        for (const InstalledMod& mod : mods)
            order += mod.descriptorRef + "\n";
        plan.configWrites.push_back({installation.userDataPath / "load_order.txt", order, "order"});
        return plan;
    }

    games::ProcessNames processNames(const GameInstallation&) const override {
        return {{kExeName}, {"Paradox Launcher.exe"}};
    }

    games::LoadOrderInfo loadOrderInfo() const override { return {"fake", false, {}}; }

    std::vector<std::string> compatibilityWarnings(const GameInstallation&,
                                                   const InstalledMod&) const override {
        return {};
    }

private:
    const FakeWorld& m_world;
};

// Runtime options for a hermetic profile: fake adapter, no real discovery,
// no process listing (launch checks then only warn), no log sink.
inline RuntimeOptions hermeticOptions(const fs::path& profile, const FakeAdapter& adapter) {
    RuntimeOptions options;
    options.profileDir = profile;
    options.installLogSink = false;
    options.adapters = std::vector<const games::GameAdapter*>{&adapter};
    games::DiscoveryEnvironment env;
    env.home = profile;
    env.documentsDir = profile;
    env.xdgDataHome = profile;
    options.environment = env;
    options.configureContext = [](services::ServiceContext& context) {
        context.startProcess =
            [](const platform::ProcessSpec&) -> Result<platform::StartedProcess> {
            return makeError(ErrorCode::ProcessStartFailed, "tests never start processes");
        };
        context.listProcesses = [] {
            return Result<std::vector<platform::RunningProcess>>(
                std::vector<platform::RunningProcess>{});
        };
    };
    return options;
}

} // namespace pmm::app::test
