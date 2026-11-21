#include "providers/web_metadata.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::providers;

namespace {

std::string readFixture(const char* relative) {
    std::ifstream in(std::filesystem::path(PMM_FIXTURES_DIR) / relative, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

} // namespace

TEST_CASE("Details request body encodes item ids in order", "[providers][web]") {
    const std::vector<std::string> ids{"1200000019", "1200000023"};
    auto body = buildPublishedFileDetailsRequest(ids);
    REQUIRE(body.ok());
    CHECK(*body ==
          "itemcount=2&publishedfileids%5B0%5D=1200000019&publishedfileids%5B1%5D=1200000023");
    CHECK(kPublishedFileDetailsUrl ==
          "https://api.steampowered.com/ISteamRemoteStorage/GetPublishedFileDetails/v1/");
    CHECK(kPublishedFileDetailsContentType == "application/x-www-form-urlencoded");
}

TEST_CASE("Details request rejects empty, oversized and invalid id lists", "[providers][web]") {
    CHECK_FALSE(buildPublishedFileDetailsRequest({}).ok());

    std::vector<std::string> many(kMaxPublishedFileDetailsItems + 1, "1");
    auto tooMany = buildPublishedFileDetailsRequest(many);
    REQUIRE_FALSE(tooMany.ok());
    CHECK(tooMany.error().code == ErrorCode::InvalidArgument);

    std::vector<std::string> exactlyMax(kMaxPublishedFileDetailsItems, "7");
    CHECK(buildPublishedFileDetailsRequest(exactlyMax).ok());

    for (const char* bad : {"12&key=secret", "0", "", "1e5", "%31"}) {
        INFO(bad);
        const std::vector<std::string> ids{"1", bad};
        auto r = buildPublishedFileDetailsRequest(ids);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().code == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("Details response fixture parses found, missing, wrong-game and banned items",
          "[providers][web][fixture]") {
    const std::string body = readFixture("providers/published_file_details_response.json");
    REQUIRE_FALSE(body.empty());
    auto parsed = parsePublishedFileDetailsResponse(body);
    REQUIRE(parsed.ok());
    const auto& items = *parsed;
    REQUIRE(items.size() == 5);

    const WorkshopItemMetadata& stellaris = items[0];
    CHECK(stellaris.id == "1200000019");
    CHECK(stellaris.found);
    CHECK(stellaris.resultCode == 1);
    CHECK(stellaris.title == "Example Mod D");
    CHECK(stellaris.consumerAppId == "281990");
    CHECK(stellaris.creatorAppId == "281990");
    CHECK(stellaris.fileSize == 300000); // a string in the reply
    CHECK(stellaris.timeCreated == 1440000000);
    CHECK(stellaris.timeUpdated == 1708000000);
    CHECK(stellaris.contentManifest == "1100000000000000104"); // hcontent_file
    CHECK_FALSE(stellaris.banned);
    CHECK(stellaris.visibility == static_cast<int>(WorkshopVisibility::Public));
    CHECK(stellaris.tags == std::vector<std::string>{"Gameplay", "Graphics"});
    CHECK(validateWorkshopItemForGame(stellaris, "281990").ok());

    const WorkshopItemMetadata& missing = items[1];
    CHECK(missing.id == "1");
    CHECK_FALSE(missing.found);
    CHECK(missing.resultCode == 9);
    CHECK(missing.title.empty());
    auto notFound = validateWorkshopItemForGame(missing, "281990");
    REQUIRE_FALSE(notFound.ok());
    CHECK(notFound.error().code == ErrorCode::ItemUnavailable);
    CHECK(notFound.error().message.find("not found") != std::string::npos);

    const WorkshopItemMetadata& hoi = items[2];
    CHECK(hoi.found);
    CHECK(hoi.consumerAppId == "394360");
    auto wrongGame = validateWorkshopItemForGame(hoi, "281990");
    REQUIRE_FALSE(wrongGame.ok());
    CHECK(wrongGame.error().code == ErrorCode::WrongGame);
    CHECK(wrongGame.error().message.find("394360") != std::string::npos);
    CHECK(validateWorkshopItemForGame(hoi, "394360").ok());

    const WorkshopItemMetadata& banned = items[3];
    CHECK(banned.fileSize == 2048);          // a number here
    CHECK(banned.timeUpdated == 1700000001); // numeric string here
    CHECK(banned.banned);
    CHECK(banned.banReason == "Example reason");
    CHECK(banned.visibility == static_cast<int>(WorkshopVisibility::Private));
    auto bannedCheck = validateWorkshopItemForGame(banned, "281990");
    REQUIRE_FALSE(bannedCheck.ok());
    CHECK(bannedCheck.error().code == ErrorCode::ItemUnavailable);
    CHECK(bannedCheck.error().detail == "Example reason");

    const WorkshopItemMetadata& denied = items[4];
    CHECK(denied.resultCode == 15);
    auto deniedCheck = validateWorkshopItemForGame(denied, "281990");
    REQUIRE_FALSE(deniedCheck.ok());
    CHECK(deniedCheck.error().code == ErrorCode::ItemUnavailable);
    CHECK(deniedCheck.error().detail == "EResult 15 (AccessDenied)");
}

TEST_CASE("Each item of the fixture gets its verdict, and the messages stay as they were",
          "[providers][web][fixture]") {
    auto parsed = parsePublishedFileDetailsResponse(
        readFixture("providers/published_file_details_response.json"));
    REQUIRE(parsed.ok());
    const auto& items = *parsed;
    REQUIRE(items.size() == 5);
    using enum WorkshopItemVerdict;
    CHECK(classifyWorkshopItemForGame(items[0], "281990") == Ok);
    CHECK(classifyWorkshopItemForGame(items[1], "281990") == NotFound);
    CHECK(classifyWorkshopItemForGame(items[2], "281990") == WrongGame);
    CHECK(classifyWorkshopItemForGame(items[2], "394360") == Ok);
    CHECK(classifyWorkshopItemForGame(items[3], "281990") == Banned);
    CHECK(classifyWorkshopItemForGame(items[4], "281990") == NoDetails);
    // The order of the checks: not found before banned, banned before another game.
    WorkshopItemMetadata item = items[3];
    CHECK(classifyWorkshopItemForGame(item, "394360") == Banned);
    item.resultCode = 9;
    item.found = false;
    CHECK(classifyWorkshopItemForGame(item, "281990") == NotFound);
    item.resultCode = 15;
    CHECK(classifyWorkshopItemForGame(item, "281990") == NoDetails);

    // validateWorkshopItemForGame() keeps its exact messages.
    const auto error = [&](std::size_t index) {
        const Status status = validateWorkshopItemForGame(items[index], "281990");
        REQUIRE_FALSE(status.ok());
        return status.error();
    };
    Error e = error(1);
    CHECK(e.code == ErrorCode::ItemUnavailable);
    CHECK(e.message == "Workshop item 1 was not found. It may have been deleted, or it is private "
                       "or friends-only.");
    CHECK(e.detail == "EResult 9 (FileNotFound)");
    e = error(2);
    CHECK(e.code == ErrorCode::WrongGame);
    CHECK(e.message == "Workshop item 2000000002 (Example Hearts of Iron IV mod) belongs to a "
                       "different game (Steam app 394360).");
    CHECK(e.detail == "expected app 281990");
    e = error(3);
    CHECK(e.code == ErrorCode::ItemUnavailable);
    CHECK(e.message ==
          "Workshop item 3000000003 (Example banned mod) has been banned from the Workshop.");
    CHECK(e.detail == "Example reason");
    e = error(4);
    CHECK(e.code == ErrorCode::ItemUnavailable);
    CHECK(e.message == "Steam returned no details for Workshop item 4000000004: Access to the "
                       "Workshop item was denied (it may be private, friends-only or hidden).");
    CHECK(e.detail == "EResult 15 (AccessDenied)");
    CHECK(validateWorkshopItemForGame(items[0], "281990").ok());
}

TEST_CASE("Details response parsing fails safely on malformed replies", "[providers][web]") {
    struct Row {
        const char* body;
        ErrorCode code;
    };
    const Row rows[] = {
        {"", ErrorCode::ParseError},
        {"<html>Service Unavailable</html>", ErrorCode::ParseError},
        {"[]", ErrorCode::ParseError},
        {R"({"result":1})", ErrorCode::ParseError},
        {R"({"response":[]})", ErrorCode::ParseError},
        {R"({"response":{"result":1,"publishedfiledetails":[5]}})", ErrorCode::ParseError},
        {R"({"response":{"result":1,"publishedfiledetails":[{"result":1}]}})",
         ErrorCode::ParseError},
        {R"({"response":{"result":1,"publishedfiledetails":[{"publishedfileid":"x","result":1}]}})",
         ErrorCode::ParseError},
        {R"({"response":{"result":8}})", ErrorCode::ProviderUnavailable},
    };
    for (const Row& row : rows) {
        INFO(row.body);
        auto r = parsePublishedFileDetailsResponse(row.body);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().code == row.code);
    }

    auto empty = parsePublishedFileDetailsResponse(R"({"response":{"result":1,"resultcount":0}})");
    REQUIRE(empty.ok());
    CHECK(empty->empty());

    const std::string huge(kMaxPublishedFileDetailsResponseBytes + 1, ' ');
    auto big = parsePublishedFileDetailsResponse(huge);
    REQUIRE_FALSE(big.ok());
    CHECK(big.error().code == ErrorCode::LimitExceeded);
}

TEST_CASE("Details parsing tolerates odd field types without trusting them", "[providers][web]") {
    auto r = parsePublishedFileDetailsResponse(
        R"({"response":{"result":1,"publishedfiledetails":[{"publishedfileid":"5","result":1,)"
        R"("consumer_app_id":"281990","file_size":-3,"time_updated":"soon","banned":true,)"
        R"("title":42,"tags":"none"}]}})");
    REQUIRE(r.ok());
    REQUIRE(r->size() == 1);
    const auto& item = (*r)[0];
    CHECK(item.consumerAppId == "281990");
    CHECK(item.fileSize == 0);
    CHECK(item.timeUpdated == 0);
    CHECK(item.banned);
    CHECK(item.title == "42");
    CHECK(item.tags.empty());
    CHECK(item.contentManifest.empty());

    // hcontent_file as a number, and "0" for no content.
    auto numbers = parsePublishedFileDetailsResponse(
        R"({"response":{"result":1,"publishedfiledetails":[)"
        R"({"publishedfileid":"6","result":1,"hcontent_file":1100000000000000501},)"
        R"({"publishedfileid":"7","result":1,"hcontent_file":"0"}]}})");
    REQUIRE(numbers.ok());
    REQUIRE(numbers->size() == 2);
    CHECK((*numbers)[0].contentManifest == "1100000000000000501");
    CHECK((*numbers)[1].contentManifest.empty());
}

namespace {

InstalledMod installedItem(const std::string& id, std::int64_t timeUpdated, bool fromSteam,
                           const std::string& manifest = "1100000000000000104") {
    InstalledMod mod;
    mod.identity = ModIdentity{"stellaris", source::SteamWorkshop, id};
    mod.contentUpdatedAt = timeUpdated;
    mod.contentTimeFromSteam = fromSteam;
    mod.workshopManifest = manifest;
    return mod;
}

WorkshopItemMetadata workshopItem(const std::string& id, std::int64_t timeUpdated,
                                  const std::string& manifest = "1100000000000000105") {
    WorkshopItemMetadata item;
    item.id = id;
    item.found = true;
    item.resultCode = 1;
    item.title = "Example Item";
    item.consumerAppId = "281990";
    item.creatorAppId = "281990";
    item.timeUpdated = timeUpdated;
    item.contentManifest = manifest;
    return item;
}

} // namespace

TEST_CASE("Installed items are compared with the Workshop by time_updated", "[providers][web]") {
    SECTION("a later time_updated is an update, and both manifests are recorded") {
        const auto c = compareWithWorkshop(installedItem("1200000019", 1708000000, true),
                                           workshopItem("1200000019", 1708000500), "281990");
        CHECK(c.verdict == WorkshopUpdateVerdict::UpdateAvailable);
        CHECK(c.installedTimeUpdated == 1708000000);
        CHECK(c.workshopTimeUpdated == 1708000500);
        CHECK(c.installedManifest == "1100000000000000104");
        CHECK(c.workshopManifest == "1100000000000000105");
        CHECK(c.message.empty());
        CHECK(std::string(toString(c.verdict)) == "update_available");
    }
    SECTION("the same or an older time is up to date, whatever the manifests say") {
        for (std::int64_t workshopTime : {std::int64_t{1708000000}, std::int64_t{1707000000}}) {
            const auto c = compareWithWorkshop(installedItem("1200000019", 1708000000, true),
                                               workshopItem("1200000019", workshopTime), "281990");
            CHECK(c.verdict == WorkshopUpdateVerdict::UpToDate);
        }
    }
    SECTION("a folder time is never compared") {
        // The folder was modified long before the Workshop version: a false
        // "update available" if it were compared.
        const auto c = compareWithWorkshop(installedItem("1200000019", 1600000000, false),
                                           workshopItem("1200000019", 1708000500), "281990");
        CHECK(c.verdict == WorkshopUpdateVerdict::NotCompared);
        CHECK(c.installedTimeUpdated == 0);
        CHECK(c.message.find("Steam's records") != std::string::npos);
    }
    SECTION("a reply without time_updated is not compared") {
        const auto c = compareWithWorkshop(installedItem("1200000019", 1708000000, true),
                                           workshopItem("1200000019", 0), "281990");
        CHECK(c.verdict == WorkshopUpdateVerdict::NotCompared);
    }
    SECTION("a manifest of \"0\" is unknown") {
        const auto c = compareWithWorkshop(installedItem("1200000019", 1708000000, true, "0"),
                                           workshopItem("1200000019", 1708000500, "0"), "281990");
        CHECK(c.verdict == WorkshopUpdateVerdict::UpdateAvailable);
        CHECK(c.installedManifest.empty());
        CHECK(c.workshopManifest.empty());
    }
    SECTION("removed, banned and other-game items are refused, and the copy stays") {
        WorkshopItemMetadata missing;
        missing.id = "1200000019";
        missing.resultCode = 9;
        const auto gone =
            compareWithWorkshop(installedItem("1200000019", 1708000000, true), missing, "281990");
        CHECK(gone.verdict == WorkshopUpdateVerdict::Refused);
        CHECK(gone.refusal == ErrorCode::ItemUnavailable);
        CHECK(gone.message.find("not found") != std::string::npos);
        CHECK(gone.message.find("installed copy stays") != std::string::npos);

        WorkshopItemMetadata banned = workshopItem("1200000019", 1708000500);
        banned.banned = true;
        CHECK(compareWithWorkshop(installedItem("1200000019", 1708000000, true), banned, "281990")
                  .verdict == WorkshopUpdateVerdict::Refused);

        const auto other = compareWithWorkshop(installedItem("1200000019", 1708000000, true),
                                               workshopItem("1200000019", 1708000500), "236850");
        CHECK(other.verdict == WorkshopUpdateVerdict::Refused);
        CHECK(other.refusal == ErrorCode::WrongGame);
    }
}
