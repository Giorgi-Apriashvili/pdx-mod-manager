#include "persistence/inventory_repository.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::persistence;
using namespace pmm::persistence::test;

namespace {

InstalledMod workshopMod(const std::string& id, const std::string& name,
                         const GameKey& game = "stellaris") {
    InstalledMod mod;
    mod.identity = ModIdentity{game, source::SteamWorkshop, id};
    mod.descriptor.name = name;
    mod.descriptor.version = "4.5.*";
    mod.descriptor.supportedVersion = "v4.5.*";
    mod.descriptor.tags = {"Fixes", "Graphics"};
    mod.descriptor.picture = "thumbnail.png";
    mod.descriptor.remoteFileId = id;
    mod.contentPath =
        fs::path(u8"C:/Program Files (x86)/Steam/steamapps/workshop/content/281990") / id;
    mod.descriptorPath =
        fs::path(u8"C:/Users/Example/Documents/Paradox Interactive/Stellaris/mod") /
        ("ugc_" + id + ".mod");
    mod.descriptorRef = "mod/ugc_" + id + ".mod";
    mod.contentUpdatedAt = 1761000000;
    return mod;
}

InstalledMod localMod() {
    InstalledMod mod;
    mod.identity = ModIdentity{"stellaris", source::Local, "my_local_mod"};
    mod.descriptor.name = "Ünïcødé local mod 日本語";
    mod.descriptor.version = "1.0";
    mod.descriptor.dependencies = {"UI Overhaul Dynamic", "Another \"quoted\" mod"};
    mod.descriptor.path = "C:/Users/Other Machine/Documents/mod/my_local_mod";
    mod.descriptor.archive = "legacy.zip";
    mod.descriptor.extra = {{"replace_path", "common/ethics"}, {"user_dir", "x y"}};
    mod.contentPath = fs::path(u8"C:/Users/Example/Documents/Paradox Interactive/Stellaris/mod/"
                               u8"Ünïcødé dir/日本語");
    mod.descriptorPath =
        fs::path(u8"C:/Users/Example/Documents/Paradox Interactive/Stellaris/mod/local.mod");
    mod.descriptorRef = "mod/local.mod";
    mod.state = InstallState::Broken;
    mod.problems = {"descriptor.mod is missing", "path does not exist"};
    mod.contentUpdatedAt = 0;
    mod.fingerprint = "sha256:" + std::string(64, 'a');
    return mod;
}

void checkEqual(const InstalledMod& a, const InstalledMod& b) {
    CHECK(a.identity == b.identity);
    CHECK(a.descriptor.name == b.descriptor.name);
    CHECK(a.descriptor.version == b.descriptor.version);
    CHECK(a.descriptor.supportedVersion == b.descriptor.supportedVersion);
    CHECK(a.descriptor.tags == b.descriptor.tags);
    CHECK(a.descriptor.dependencies == b.descriptor.dependencies);
    CHECK(a.descriptor.picture == b.descriptor.picture);
    CHECK(a.descriptor.path == b.descriptor.path);
    CHECK(a.descriptor.archive == b.descriptor.archive);
    CHECK(a.descriptor.remoteFileId == b.descriptor.remoteFileId);
    CHECK(a.descriptor.extra == b.descriptor.extra);
    CHECK(a.contentPath == b.contentPath);
    CHECK(a.descriptorPath == b.descriptorPath);
    CHECK(a.descriptorRef == b.descriptorRef);
    CHECK(a.state == b.state);
    CHECK(a.problems == b.problems);
    CHECK(a.contentUpdatedAt == b.contentUpdatedAt);
    CHECK(a.fingerprint == b.fingerprint);
}

std::vector<std::string> ids(const std::vector<InstalledMod>& mods) {
    std::vector<std::string> out;
    for (const auto& m : mods)
        out.push_back(m.identity.id);
    return out;
}

} // namespace

TEST_CASE("Inventory round-trips every installed-mod field", "[persistence][inventory]") {
    auto db = openMemory();
    InventoryRepository repo(*db);
    const auto workshop = workshopMod("18446744073709551615", "UI Overhaul Dynamic");
    const auto local = localMod();
    requireOk(repo.replaceForGame("stellaris", {workshop, local}));

    auto mods = require(repo.list("stellaris"));
    REQUIRE(mods.size() == 2);
    checkEqual(mods[0], local); // "local" sorts before "steam-workshop"
    checkEqual(mods[1], workshop);

    auto found = require(repo.find(workshop.identity));
    REQUIRE(found.has_value());
    checkEqual(*found, workshop);
    CHECK_FALSE(require(repo.find({"stellaris", source::SteamWorkshop, "1"})).has_value());
    CHECK_FALSE(
        require(repo.find({"eu4", source::SteamWorkshop, workshop.identity.id})).has_value());

    // Provider ids are stored as text, never as numbers.
    CHECK(queryText(*db, "SELECT typeof(provider_id) FROM installed_mods "
                         "WHERE source = 'steam-workshop'") == "text");
    CHECK(queryInt(*db, "SELECT content_updated_at FROM installed_mods "
                        "WHERE source = 'steam-workshop'") == 1761000000LL * 1000);
}

TEST_CASE("Replacing a game's inventory leaves other games alone", "[persistence][inventory]") {
    auto db = openMemory();
    InventoryRepository repo(*db);
    requireOk(repo.replaceForGame("stellaris", {workshopMod("1", "A"), workshopMod("2", "B")}));
    requireOk(repo.replaceForGame("eu4", {workshopMod("9", "EU", "eu4")}));
    requireOk(repo.replaceForGame("stellaris", {workshopMod("2", "B v2"), workshopMod("3", "C")}));

    auto stellarisMods = require(repo.list("stellaris"));
    CHECK(ids(stellarisMods) == std::vector<std::string>{"2", "3"});
    CHECK(stellarisMods[0].descriptor.name == "B v2");
    CHECK(ids(require(repo.list("eu4"))) == std::vector<std::string>{"9"});

    requireOk(repo.replaceForGame("stellaris", {}));
    CHECK(require(repo.list("stellaris")).empty());
    CHECK(require(repo.list("eu4")).size() == 1);
}

TEST_CASE("Invalid inventories are rejected without changes", "[persistence][inventory]") {
    auto db = openMemory();
    InventoryRepository repo(*db);
    requireOk(repo.replaceForGame("stellaris", {workshopMod("1", "A")}));

    auto wrongGame = repo.replaceForGame("stellaris", {workshopMod("2", "B", "eu4")});
    REQUIRE_FALSE(wrongGame.ok());
    CHECK(wrongGame.error().code == ErrorCode::InvalidArgument);

    auto duplicate =
        repo.replaceForGame("stellaris", {workshopMod("2", "B"), workshopMod("2", "B")});
    REQUIRE_FALSE(duplicate.ok());
    CHECK(duplicate.error().code == ErrorCode::InvalidArgument);

    CHECK(ids(require(repo.list("stellaris"))) == std::vector<std::string>{"1"});
}

TEST_CASE("A failure part-way through a replace keeps the previous inventory",
          "[persistence][inventory]") {
    auto db = openMemory();
    InventoryRepository repo(*db);
    requireOk(repo.replaceForGame("stellaris", {workshopMod("1", "A"), workshopMod("2", "B")}));
    // Simulate a write failure after some rows were already replaced.
    execRaw(*db, "CREATE TEMP TRIGGER fail_inventory BEFORE INSERT ON installed_mods "
                 "WHEN NEW.provider_id = 'boom' BEGIN SELECT RAISE(ABORT, 'injected'); END");

    auto status = repo.replaceForGame(
        "stellaris", {workshopMod("3", "C"), workshopMod("4", "D"), workshopMod("boom", "X")});
    REQUIRE_FALSE(status.ok());
    CHECK(status.error().detail.find("injected") != std::string::npos);

    auto mods = require(repo.list("stellaris"));
    CHECK(ids(mods) == std::vector<std::string>{"1", "2"});
    CHECK(mods[0].descriptor.name == "A");
}

#if defined(_WIN32)
TEST_CASE("A scanned path that is not valid UTF-16 fails the replace instead of throwing",
          "[persistence][inventory]") {
    auto db = openMemory();
    InventoryRepository repo(*db);
    requireOk(repo.replaceForGame("stellaris", {workshopMod("1", "A")}));

    auto badDescriptor = localMod();
    badDescriptor.descriptorPath = loneSurrogatePath(
        L"C:/Users/Example/Documents/Paradox Interactive/Stellaris/mod/bad", L".mod");
    Status status;
    REQUIRE_NOTHROW(status =
                        repo.replaceForGame("stellaris", {workshopMod("2", "B"), badDescriptor}));
    REQUIRE_FALSE(status.ok());
    CHECK(status.error().code == ErrorCode::InvalidArgument);
    CHECK(status.error().message.find("Rename") != std::string::npos);
    CHECK(status.error().detail.find("bad\xEF\xBF\xBD.mod") != std::string::npos);

    auto badContent = localMod();
    badContent.contentPath = loneSurrogatePath(L"C:/Mods/content", L"");
    REQUIRE_NOTHROW(status = repo.replaceForGame("stellaris", {badContent}));
    REQUIRE_FALSE(status.ok());
    CHECK(status.error().code == ErrorCode::InvalidArgument);

    CHECK(ids(require(repo.list("stellaris"))) == std::vector<std::string>{"1"});
}
#else
TEST_CASE("Paths that are not valid UTF-8 round-trip as their raw bytes",
          "[persistence][inventory]") {
    auto db = openMemory();
    InventoryRepository repo(*db);
    auto mod = localMod();
    mod.contentPath = fs::path("/home/example/mods/caf\xE9");
    mod.descriptorPath = fs::path("/home/example/.local/share/Paradox Interactive/Stellaris/mod/"
                                  "caf\xE8.mod");
    requireOk(repo.replaceForGame("stellaris", {mod}));
    auto mods = require(repo.list("stellaris"));
    REQUIRE(mods.size() == 1);
    CHECK(mods[0].contentPath.native() == mod.contentPath.native());
    CHECK(mods[0].descriptorPath.native() == mod.descriptorPath.native());
}
#endif
