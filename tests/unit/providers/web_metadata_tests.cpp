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
    const std::vector<std::string> ids{"703156866", "1121692237"};
    auto body = buildPublishedFileDetailsRequest(ids);
    REQUIRE(body.ok());
    CHECK(*body ==
          "itemcount=2&publishedfileids%5B0%5D=703156866&publishedfileids%5B1%5D=1121692237");
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

    const WorkshopItemMetadata& camera = items[0];
    CHECK(camera.id == "703156866");
    CHECK(camera.found);
    CHECK(camera.resultCode == 1);
    CHECK(camera.title == "Cinematic Camera");
    CHECK(camera.consumerAppId == "281990");
    CHECK(camera.creatorAppId == "281990");
    CHECK(camera.fileSize == 429416); // a string in the reply
    CHECK(camera.timeCreated == 1465842499);
    CHECK(camera.timeUpdated == 1782489192);
    CHECK_FALSE(camera.banned);
    CHECK(camera.visibility == static_cast<int>(WorkshopVisibility::Public));
    CHECK(camera.tags == std::vector<std::string>{"Gameplay", "Camera"});
    CHECK(validateWorkshopItemForGame(camera, "281990").ok());

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
}
