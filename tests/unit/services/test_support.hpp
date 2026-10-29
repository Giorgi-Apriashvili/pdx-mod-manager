#pragma once

// Shared fixtures for the services tests: a hermetic world (temporary folders
// whose names contain spaces and non-ASCII characters, an in-memory database
// on a fake clock, a configuration-transaction journal), a fake game adapter
// that scans plain folders, a fake Steam helper host that simulates
// subscribe/download, a fake Web API, and fake process starter/lister.
// Nothing here touches a real game, Steam folder, process or the network.

#include "core/model.hpp"
#include "games/game_adapter.hpp"
#include "persistence/database.hpp"
#include "persistence/testing.hpp"
#include "platform/file_transaction.hpp"
#include "platform/fs.hpp"
#include "platform/steam.hpp"
#include "providers/helper_protocol.hpp"
#include "providers/local_fingerprint.hpp"
#include "services/acquisition_coordinator.hpp"
#include "services/game_service.hpp"
#include "services/service_context.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace pmm::services::test {

namespace fs = std::filesystem;

inline constexpr const char* kGame = "stellaris";
inline constexpr const char* kAppId = "281990";
inline constexpr const char* kExeName = "fakegame.exe";
inline constexpr const char* kLauncherName = "Paradox Launcher.exe";

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
    REQUIRE(platform::writeFileAtomic(path, text).ok());
}

inline std::string readText(const fs::path& path) {
    auto contents = platform::readFile(path);
    REQUIRE(contents.ok());
    return contents.value();
}

inline fs::path fixture(std::string_view relative) {
    return u8(PMM_FIXTURES_DIR) / "services" / u8(relative);
}

// Unique folder "<temp>/pmm services Ünïcødé <stamp>-<n>", removed on destruction.
class TempDir {
public:
    TempDir() {
        static std::atomic<unsigned> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = fs::temp_directory_path() / u8("pmm services Ünïcødé " + std::to_string(stamp) +
                                                "-" + std::to_string(counter++));
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

// Reads name, version, supported_version and dependencies from a descriptor.
inline ModDescriptor parseDescriptor(const std::string& text) {
    ModDescriptor d;
    const auto value = [&](const std::string& key) -> std::string {
        const std::string prefix = key + "=\"";
        std::size_t pos = 0;
        while ((pos = text.find(prefix, pos)) != std::string::npos) {
            if (pos == 0 || text[pos - 1] == '\n') {
                const std::size_t start = pos + prefix.size();
                return text.substr(start, text.find('"', start) - start);
            }
            pos += prefix.size();
        }
        return {};
    };
    d.name = value("name");
    d.version = value("version");
    d.supportedVersion = value("supported_version");
    d.remoteFileId = value("remote_file_id");
    if (const std::size_t block = text.find("dependencies={"); block != std::string::npos) {
        const std::size_t end = text.find('}', block);
        std::size_t pos = block;
        while ((pos = text.find('"', pos)) != std::string::npos && pos < end) {
            const std::size_t close = text.find('"', pos + 1);
            d.dependencies.push_back(text.substr(pos + 1, close - pos - 1));
            pos = close + 1;
        }
    }
    return d;
}

// Game adapter double for "stellaris": Workshop mods are the folders of
// <library>/steamapps/workshop/content/281990, local mods the folders of
// <userData>/mod. The launch plan writes <userData>/dlc_load.json.
class FakeAdapter : public games::GameAdapter {
public:
    std::vector<GameInstallation> discovered;
    std::optional<Error> scanError;
    std::optional<Error> planError;
    std::vector<PlaysetEntry> currentConfiguration;
    std::string loadOrderNote;
    mutable int scans = 0;
    mutable std::vector<std::vector<ModIdentity>> launchRequests;

    const GameCatalogEntry& catalog() const override { return *findGame(kGame); }

    Capabilities capabilities(const GameInstallation&) const override {
        return Capabilities{true, true, true};
    }

    std::vector<GameInstallation> discover(const games::DiscoveryEnvironment&) const override {
        return discovered;
    }

    Result<GameInstallation>
    inspectInstallation(const fs::path& installPath, const fs::path& userDataPath,
                        const games::DiscoveryEnvironment& env) const override {
        std::error_code ec;
        if (!fs::is_regular_file(installPath / kExeName, ec))
            return makeError(ErrorCode::ValidationFailed,
                             "This folder does not contain the game executable.",
                             platform::pathToUtf8(installPath));
        GameInstallation g;
        g.game = kGame;
        g.installPath = installPath;
        g.userDataPath = userDataPath.empty()
                             ? env.documentsDir / "Paradox Interactive" / "Stellaris"
                             : userDataPath;
        g.executablePath = installPath / kExeName;
        g.defaultArguments = {"-gdpr-compliant"};
        g.distribution = DistributionSource::Manual;
        g.steamAppId = kAppId;
        // <library>/steamapps/common/<game>
        if (installPath.parent_path().filename() == "common")
            g.steamLibraryPath = installPath.parent_path().parent_path().parent_path();
        g.version = "v4.5.2";
        g.versionLabel = "Fake v4.5.2 (manual)";
        return g;
    }

    Result<std::vector<InstalledMod>>
    scanMods(const GameInstallation& installation) const override {
        ++scans;
        if (scanError)
            return *scanError;
        std::vector<InstalledMod> mods;
        const auto scanFolder = [&](const fs::path& folder, const std::string& sourceKey) {
            std::error_code ec;
            for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_directory())
                    continue;
                InstalledMod mod;
                const std::string id = platform::pathToUtf8(it->path().filename());
                mod.identity = ModIdentity{kGame, sourceKey, id};
                mod.contentPath = it->path();
                mod.descriptorRef = sourceKey == source::SteamWorkshop ? "mod/ugc_" + id + ".mod"
                                                                       : "mod/" + id + ".mod";
                mod.contentUpdatedAt = platform::lastWriteUnixSeconds(it->path());
                auto text = platform::readFile(it->path() / "descriptor.mod");
                if (text && !text.value().empty()) {
                    mod.descriptor = parseDescriptor(text.value());
                } else {
                    mod.state = InstallState::Broken;
                    mod.problems.push_back("descriptor.mod is missing");
                }
                mods.push_back(std::move(mod));
            }
        };
        if (!installation.steamLibraryPath.empty())
            scanFolder(platform::steam::workshopContentDir(installation.steamLibraryPath, kAppId),
                       source::SteamWorkshop);
        scanFolder(installation.userDataPath / "mod", source::Local);
        return mods;
    }

    Result<std::vector<PlaysetEntry>>
    readCurrentConfiguration(const GameInstallation&,
                             std::span<const InstalledMod>) const override {
        return currentConfiguration;
    }

    Result<LaunchPlan> buildLaunchPlan(const GameInstallation& installation,
                                       std::span<const InstalledMod> mods) const override {
        std::vector<ModIdentity> identities;
        for (const InstalledMod& mod : mods)
            identities.push_back(mod.identity);
        launchRequests.push_back(identities);
        if (planError)
            return *planError;
        LaunchPlan plan;
        plan.game = kGame;
        plan.executable = installation.executablePath;
        plan.workingDirectory = installation.installPath;
        plan.arguments = installation.defaultArguments;
        plan.environment = {{"SteamAppId", kAppId}};
        std::string json = "{\"enabled_mods\":[";
        for (std::size_t i = 0; i < mods.size(); ++i)
            json += (i ? ",\"" : "\"") + mods[i].descriptorRef + "\"";
        json += "],\"disabled_dlcs\":[]}";
        plan.configWrites.push_back(
            ConfigWrite{installation.userDataPath / "dlc_load.json", json, "load order"});
        return plan;
    }

    games::ProcessNames processNames(const GameInstallation&) const override {
        return games::ProcessNames{{kExeName}, {kLauncherName}};
    }

    games::LoadOrderInfo loadOrderInfo() const override {
        return games::LoadOrderInfo{"index 0 first (fake)", false, {}, loadOrderNote};
    }

    std::vector<std::string> compatibilityWarnings(const GameInstallation& installation,
                                                   const InstalledMod& mod) const override {
        const std::string& wanted = mod.descriptor.supportedVersion;
        if (wanted.empty() || wanted.size() < 3 || installation.version.size() < 3)
            return {};
        // "v4.5.*" matches "v4.5.2"
        const std::string prefix = wanted.substr(0, wanted.find('*'));
        if (installation.version.rfind(prefix, 0) == 0)
            return {};
        return {"Made for " + wanted + "; the installed game is " + installation.version + "."};
    }
};

// Simulated pmm-steam-helper session for kGame. Answers commands by queueing
// events; pump() delivers them to the coordinator (never re-entrantly).
class FakeSteam : public WorkshopSessionHost {
public:
    struct Item {
        std::uint32_t flags = 0;
        std::uint64_t total = 4000;
        std::uint64_t downloaded = 0;
        int subscribeResult = 1; // EResult
        bool acceptDownload = true;
        int steps = 2; // state polls while downloading
        bool writeContent = true;
        std::string name = "Downloaded Example";
    };
    struct Exit {
        int code = 0;
    };

    explicit FakeSteam(fs::path workshopDir) : m_workshop(std::move(workshopDir)) {
        ready.ok = true;
        ready.loggedOn = true;
        ready.ownsApp = true;
        ready.appId = kAppId;
    }

    std::map<std::string, Item> items;
    providers::ReadyEvent ready;
    std::optional<int> exitBeforeReady;
    bool openSucceeds = true;

    int opens = 0;
    int closes = 0;
    bool isOpen = false;
    std::vector<std::string> openedAppIds;
    std::vector<fs::path> openedLibraries;
    std::vector<providers::HelperCommand> sent;
    std::deque<std::variant<providers::HelperEvent, Exit>> pending;

    bool open(const GameKey&, const std::string& appId, const fs::path& library) override {
        ++opens;
        openedAppIds.push_back(appId);
        openedLibraries.push_back(library);
        if (!openSucceeds)
            return false;
        isOpen = true;
        if (exitBeforeReady)
            pending.emplace_back(Exit{*exitBeforeReady});
        else
            pending.emplace_back(providers::HelperEvent{ready});
        return true;
    }

    void send(const GameKey&, const providers::HelperCommand& command) override {
        REQUIRE(isOpen);
        sent.push_back(command);
        using namespace providers;
        if (const auto* state = std::get_if<StateCommand>(&command)) {
            for (const std::string& id : state->ids) {
                Item& item = items[id];
                advance(id, item);
                pending.emplace_back(HelperEvent{ItemStateEvent{status(id, item)}});
            }
        } else if (const auto* subscribe = std::get_if<SubscribeCommand>(&command)) {
            Item& item = items[subscribe->id];
            if (item.subscribeResult == 1)
                item.flags |= item_flag::Subscribed;
            pending.emplace_back(HelperEvent{SubscribeResultEvent{
                subscribe->id, item.subscribeResult == 1, item.subscribeResult}});
        } else if (const auto* download = std::get_if<DownloadCommand>(&command)) {
            Item& item = items[download->id];
            if (item.acceptDownload)
                item.flags |= item_flag::DownloadPending;
            pending.emplace_back(
                HelperEvent{DownloadRequestedEvent{download->id, item.acceptDownload}});
        } else if (std::holds_alternative<PingCommand>(command)) {
            pending.emplace_back(HelperEvent{PongEvent{}});
        }
    }

    void close(const GameKey&) override {
        ++closes;
        isOpen = false;
        pending.clear();
    }

    // Delivers queued events (and an exit) until none are left.
    std::vector<CoordinatorUpdate> pump(AcquisitionCoordinator& coordinator) {
        std::vector<CoordinatorUpdate> updates;
        for (int guard = 0; guard < 10000 && !pending.empty(); ++guard) {
            auto next = std::move(pending.front());
            pending.pop_front();
            if (auto* event = std::get_if<providers::HelperEvent>(&next)) {
                updates.push_back(coordinator.onHelperEvent(kGame, *event));
            } else {
                isOpen = false;
                updates.push_back(coordinator.onHelperExited(kGame, std::get<Exit>(next).code));
            }
        }
        return updates;
    }

    template <class Command> std::vector<std::string> sentIds() const {
        std::vector<std::string> ids;
        for (const providers::HelperCommand& command : sent) {
            if (const auto* c = std::get_if<Command>(&command)) {
                if constexpr (std::is_same_v<Command, providers::StateCommand>)
                    ids.insert(ids.end(), c->ids.begin(), c->ids.end());
                else
                    ids.push_back(c->id);
            }
        }
        return ids;
    }

private:
    void advance(const std::string& id, Item& item) {
        using namespace providers::item_flag;
        if (item.flags & DownloadPending) {
            item.flags = (item.flags & ~DownloadPending) | Downloading;
            return;
        }
        if (item.flags & Downloading) {
            item.downloaded = std::min(
                item.total, item.downloaded + item.total / static_cast<std::uint64_t>(item.steps));
            if (item.downloaded >= item.total) {
                item.flags = (item.flags & ~Downloading) | Installed;
                if (item.writeContent)
                    writeText(m_workshop / u8(id) / "descriptor.mod",
                              "name=\"" + item.name + "\"\nremote_file_id=\"" + id + "\"\n");
            }
        }
    }

    providers::ItemStatus status(const std::string& id, const Item& item) const {
        providers::ItemStatus s;
        s.id = id;
        s.flags = item.flags;
        if (item.flags & providers::item_flag::Downloading) {
            s.bytesDownloaded = item.downloaded;
            s.bytesTotal = item.total;
        }
        if (item.flags & providers::item_flag::Installed)
            s.installPath = platform::pathToUtf8(m_workshop / u8(id));
        return s;
    }

    fs::path m_workshop;
};

// Simulated Web API. Every request is recorded; answer() replies to the
// outstanding ones from `items` (default: a public item of kAppId) while
// `answering` is set. Unanswered requests model an unreachable Web API.
class FakeMetadata : public WorkshopMetadataHost {
public:
    std::vector<std::vector<std::string>> requests;
    std::deque<std::vector<std::string>> unanswered;
    std::map<std::string, providers::WorkshopItemMetadata> items;
    bool answering = true;

    void request(const GameKey&, const std::vector<std::string>& ids) override {
        requests.push_back(ids);
        unanswered.push_back(ids);
    }

    providers::WorkshopItemMetadata item(const std::string& id) const {
        if (const auto it = items.find(id); it != items.end())
            return it->second;
        providers::WorkshopItemMetadata m;
        m.id = id;
        m.found = true;
        m.resultCode = 1;
        m.consumerAppId = kAppId;
        m.creatorAppId = kAppId;
        return m;
    }

    std::vector<CoordinatorUpdate> answer(AcquisitionCoordinator& coordinator) {
        std::vector<CoordinatorUpdate> updates;
        while (answering && !unanswered.empty()) {
            const std::vector<std::string> ids = std::move(unanswered.front());
            unanswered.pop_front();
            std::vector<providers::WorkshopItemMetadata> reply;
            for (const std::string& id : ids)
                reply.push_back(item(id));
            updates.push_back(coordinator.onMetadata(kGame, reply));
        }
        return updates;
    }
};

// A Steam library with the game installed, a user-data folder and an
// application profile, all under one temporary folder.
struct World {
    TempDir temp;
    fs::path root = temp.path();
    fs::path library = root / u8("Steam Library");
    fs::path install = library / "steamapps" / "common" / u8("Fake Gäme");
    fs::path executable = install / kExeName;
    fs::path documents = root / u8("Ünïcødé dir") / u8("日本語 Documents");
    fs::path userData = documents / "Paradox Interactive" / "Stellaris";
    fs::path workshop = platform::steam::workshopContentDir(library, kAppId);
    fs::path profile = root / u8("profile dir");

    std::int64_t clockMs = 1'750'000'000'000;
    std::unique_ptr<persistence::Database> db = require(persistence::Database::openInMemory());
    platform::FileTransactionManager transactions{profile / "transactions"};
    FakeAdapter adapter;
    ServiceContext ctx{*db, transactions};

    std::vector<platform::ProcessSpec> started;
    std::optional<Error> startFailure;
    std::vector<platform::RunningProcess> processes;
    int fingerprintCalls = 0;

    World() {
        writeText(executable, "placeholder, never executed");
        writeText(install / platform::steam::steamApiLibraryName(), "placeholder, never loaded");
        fs::create_directories(userData / "mod");
        fs::create_directories(workshop);
        persistence::testing::setClock(*db, [this] { return clockMs; });
        ctx.adapters = {&adapter};
        ctx.environment.documentsDir = documents;
        ctx.environment.home = root;
        ctx.environment.steamRoot = library;
        ctx.paths = AppPaths::fromProfileDir(profile);
        ctx.clock = [this] { return clockMs; };
        ctx.startProcess =
            [this](const platform::ProcessSpec& spec) -> Result<platform::StartedProcess> {
            started.push_back(spec);
            if (startFailure)
                return *startFailure;
            return platform::StartedProcess{4242};
        };
        ctx.listProcesses = [this]() -> Result<std::vector<platform::RunningProcess>> {
            return processes;
        };
        ctx.fingerprint = [this](const fs::path& folder) {
            ++fingerprintCalls;
            return providers::fingerprintDirectory(folder);
        };
        adapter.discovered = {installation()};
    }
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    GameInstallation installation() const {
        GameInstallation g;
        g.game = kGame;
        g.installPath = install;
        g.userDataPath = userData;
        g.executablePath = executable;
        g.defaultArguments = {"-gdpr-compliant"};
        g.distribution = DistributionSource::Steam;
        g.steamAppId = kAppId;
        g.steamLibraryPath = library;
        g.version = "v4.5.2";
        g.versionLabel = "Fake v4.5.2 (1234)";
        return g;
    }

    // Copies the fixture mods (three Workshop items, one local mod).
    void installFixtureMods() {
        fs::copy(fixture("workshop"), workshop, fs::copy_options::recursive);
        fs::copy(fixture("local"), userData / "mod", fs::copy_options::recursive);
    }

    void addWorkshopMod(const std::string& id, const std::string& name) {
        writeText(workshop / u8(id) / "descriptor.mod", "name=\"" + name + "\"\n");
    }

    GameState refresh() {
        GameService games(ctx);
        return require(games.refreshGame(kGame));
    }
};

inline PlaysetEntry workshopEntry(const std::string& id, const std::string& name,
                                  bool enabled = true) {
    PlaysetEntry e;
    e.identity = ModIdentity{kGame, source::SteamWorkshop, id};
    e.enabled = enabled;
    e.nameHint = name;
    return e;
}

inline PlaysetEntry localEntry(const std::string& id, const std::string& name,
                               bool enabled = true) {
    PlaysetEntry e;
    e.identity = ModIdentity{kGame, source::Local, id};
    e.enabled = enabled;
    e.nameHint = name;
    return e;
}

} // namespace pmm::services::test
