// QtWorkshopMetadataHost against a local HTTP stub (QTcpServer, http_stub.hpp):
// the request body, a parsed answer, HTTP errors, malformed replies and
// timeouts, for the coordinator's requests and the import preview's lookups.
// No request leaves the machine.

#include "app/core/metadata_host.hpp"
#include "http_stub.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

using namespace pmm;
using namespace pmm::app;
using namespace pmm::app::test;

namespace {

const QByteArray kReply = R"({"response":{"result":1,"resultcount":1,"publishedfiledetails":[
{"publishedfileid":"123456789","result":1,"creator":"76561190000000001","creator_app_id":281990,
"consumer_app_id":281990,"filename":"","file_size":"2048","title":"Example Workshop Mod",
"time_created":1465842499,"time_updated":1782489192,"visibility":0,"banned":0,"ban_reason":"",
"tags":[{"tag":"Gameplay"}]}]}})";

struct Capture {
    int answers = 0;
    std::vector<providers::WorkshopItemMetadata> items;
    std::optional<bool> finished;
};

void attach(QtWorkshopMetadataHost& host, Capture& capture) {
    host.setSink(
        [&capture](const GameKey& game, const std::vector<providers::WorkshopItemMetadata>& items) {
            CHECK(game == "stellaris");
            ++capture.answers;
            capture.items = items;
        });
    QObject::connect(&host, &QtWorkshopMetadataHost::requestFinished,
                     [&capture](bool answered) { capture.finished = answered; });
}

} // namespace

TEST_CASE("metadata host posts the request and delivers parsed items", "[app][metadata]") {
    HttpStub stub;
    stub.response = HttpStub::http(200, kReply);
    QtWorkshopMetadataHost host;
    host.setEndpoint(stub.url());
    Capture capture;
    attach(host, capture);

    host.request("stellaris", {"123456789"});
    REQUIRE(waitUntil([&] { return capture.finished.has_value(); }));
    CHECK(*capture.finished);
    REQUIRE(capture.answers == 1);
    REQUIRE(capture.items.size() == 1);
    CHECK(capture.items[0].id == "123456789");
    CHECK(capture.items[0].found);
    CHECK(capture.items[0].title == "Example Workshop Mod");
    CHECK(capture.items[0].consumerAppId == "281990");
    CHECK(host.pendingRequests() == 0);

    CHECK(stub.lastRequest.startsWith("POST /ISteamRemoteStorage/GetPublishedFileDetails/v1/"));
    CHECK(stub.lastRequest.contains("application/x-www-form-urlencoded"));
    CHECK(stub.lastRequest.endsWith("itemcount=1&publishedfileids%5B0%5D=123456789"));
    CHECK_FALSE(stub.lastRequest.toLower().contains("key="));
    CHECK_FALSE(stub.lastRequest.toLower().contains("cookie"));
}

TEST_CASE("metadata host leaves failed requests unanswered", "[app][metadata]") {
    HttpStub stub;
    QtWorkshopMetadataHost host;
    host.setEndpoint(stub.url());
    Capture capture;
    attach(host, capture);

    SECTION("HTTP error") {
        stub.response = HttpStub::http(500, "{}");
    }
    SECTION("malformed reply") {
        stub.response = HttpStub::http(200, "not json");
    }
    SECTION("timeout") {
        host.setTimeout(std::chrono::milliseconds(300));
    }

    host.request("stellaris", {"123456789"});
    REQUIRE(waitUntil([&] { return capture.finished.has_value(); }, std::chrono::seconds(10)));
    CHECK_FALSE(*capture.finished);
    CHECK(capture.answers == 0);
    CHECK(host.pendingRequests() == 0);
}

TEST_CASE("metadata host refuses invalid ids without a request", "[app][metadata]") {
    HttpStub stub;
    stub.response = HttpStub::http(200, kReply);
    QtWorkshopMetadataHost host;
    host.setEndpoint(stub.url());
    host.request("stellaris", {"not-an-id"});
    host.request("stellaris", {});
    CHECK(host.pendingRequests() == 0);
    QCoreApplication::processEvents();
    CHECK(stub.requests == 0);
}

TEST_CASE("metadata host endpoint can be overridden by the environment", "[app][metadata]") {
    qputenv("PMM_WORKSHOP_METADATA_URL", "http://127.0.0.1:1/override");
    CHECK(QtWorkshopMetadataHost::defaultEndpoint() == QUrl("http://127.0.0.1:1/override"));
    qunsetenv("PMM_WORKSHOP_METADATA_URL");
    CHECK(QtWorkshopMetadataHost::defaultEndpoint().host() ==
          QStringLiteral("api.steampowered.com"));
}

// --- lookup(): the import preview's requests ----------------------------------

namespace {

struct Lookups {
    int answers = 0;
    std::optional<WorkshopLookupReply> reply;
    int sinkCalls = 0;
};

// Calls lookup() and checks that it does not answer from inside the call.
void lookUp(QtWorkshopMetadataHost& host, Lookups& lookups, const std::vector<std::string>& ids) {
    host.setSink([&lookups](const GameKey&, const std::vector<providers::WorkshopItemMetadata>&) {
        ++lookups.sinkCalls;
    });
    host.lookup("stellaris", ids, [&lookups](WorkshopLookupReply reply) {
        ++lookups.answers;
        lookups.reply = std::move(reply);
    });
    CHECK(lookups.answers == 0); // never from inside lookup()
}

} // namespace

TEST_CASE("metadata host answers a lookup once, with the items, and never through the sink",
          "[app][metadata]") {
    HttpStub stub;
    stub.response = HttpStub::http(200, kReply);
    QtWorkshopMetadataHost host;
    host.setEndpoint(stub.url());
    Lookups lookups;
    lookUp(host, lookups, {"123456789"});
    REQUIRE(waitUntil([&] { return lookups.answers > 0; }));
    // Nothing more arrives.
    waitUntil([] { return false; }, std::chrono::milliseconds(200));
    CHECK(lookups.answers == 1);
    CHECK(lookups.sinkCalls == 0);
    REQUIRE(lookups.reply->answered);
    CHECK(lookups.reply->error.empty());
    REQUIRE(lookups.reply->items.size() == 1);
    CHECK(lookups.reply->items[0].title == "Example Workshop Mod");
    CHECK(stub.bodies == QList<QByteArray>{"itemcount=1&publishedfileids%5B0%5D=123456789"});
    CHECK_FALSE(stub.lastRequest.toLower().contains("cookie"));
    CHECK(host.pendingRequests() == 0);

    // The coordinator's requests still go to the sink.
    host.request("stellaris", {"123456789"});
    REQUIRE(waitUntil([&] { return lookups.sinkCalls == 1; }));
    CHECK(lookups.answers == 1);
}

TEST_CASE("metadata host answers a failed lookup once, unanswered", "[app][metadata]") {
    HttpStub stub;
    QtWorkshopMetadataHost host;
    host.setEndpoint(stub.url());
    std::vector<std::string> ids{"123456789"};
    int expectedRequests = 1;

    SECTION("HTTP error") {
        stub.response = HttpStub::http(500, "{}");
    }
    SECTION("malformed reply") {
        stub.response = HttpStub::http(200, "not json");
    }
    SECTION("timeout") {
        host.setTimeout(std::chrono::milliseconds(300));
    }
    SECTION("invalid ids, without a request") {
        stub.response = HttpStub::http(200, kReply);
        ids = {"not-an-id"};
        expectedRequests = 0;
    }
    SECTION("no ids, without a request") {
        stub.response = HttpStub::http(200, kReply);
        ids.clear();
        expectedRequests = 0;
    }

    Lookups lookups;
    lookUp(host, lookups, ids);
    REQUIRE(waitUntil([&] { return lookups.answers > 0; }, std::chrono::seconds(10)));
    waitUntil([] { return false; }, std::chrono::milliseconds(200));
    CHECK(lookups.answers == 1);
    CHECK(lookups.sinkCalls == 0);
    CHECK_FALSE(lookups.reply->answered);
    CHECK(lookups.reply->items.empty());
    CHECK_FALSE(lookups.reply->error.empty());
    CHECK(stub.requests == expectedRequests);
    CHECK(host.pendingRequests() == 0);
}

TEST_CASE("metadata host answers its waiting lookups when it is destroyed", "[app][metadata]") {
    HttpStub stub;
    stub.response = HttpStub::http(200, kReply);
    stub.hold();
    Lookups lookups;
    {
        QtWorkshopMetadataHost host;
        host.setEndpoint(stub.url());
        SECTION("a request in flight") {
            lookUp(host, lookups, {"123456789"});
            REQUIRE(waitUntil([&] { return stub.waiting() == 1; }));
        }
        SECTION("ids no request can be built for") {
            lookUp(host, lookups, {"not-an-id"});
        }
        CHECK(lookups.answers == 0);
    }
    CHECK(lookups.answers == 1);
    REQUIRE(lookups.reply.has_value());
    CHECK_FALSE(lookups.reply->answered);
    CHECK_FALSE(lookups.reply->error.empty());
    // Nothing more arrives, not even once the stub answers.
    stub.release();
    waitUntil([] { return false; }, std::chrono::milliseconds(200));
    CHECK(lookups.answers == 1);
}
