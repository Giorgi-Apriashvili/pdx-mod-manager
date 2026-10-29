// QtWorkshopMetadataHost against a local HTTP stub (QTcpServer): the request
// body, a parsed answer, HTTP errors, malformed replies and timeouts. No
// request leaves the machine.

#include "app/core/metadata_host.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QTcpServer>
#include <QTcpSocket>

#include <memory>
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

// Answers every HTTP request with a canned response (or never answers).
class HttpStub : public QObject {
public:
    std::optional<QByteArray> response; // full HTTP response; nullopt: never answer
    QByteArray lastRequest;
    int requests = 0;

    HttpStub() {
        REQUIRE(m_server.listen(QHostAddress::LocalHost, 0));
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
                    if (headerEnd < 0)
                        return;
                    qsizetype length = 0;
                    for (const QByteArray& line : buffer->left(headerEnd).split('\n')) {
                        if (line.toLower().startsWith("content-length:"))
                            length = line.mid(15).trimmed().toLongLong();
                    }
                    if (buffer->size() < headerEnd + 4 + length)
                        return;
                    lastRequest = *buffer;
                    ++requests;
                    if (response) {
                        socket->write(*response);
                        socket->disconnectFromHost();
                    }
                });
            }
        });
    }

    QUrl url() const {
        return QUrl(
            QStringLiteral("http://127.0.0.1:%1/ISteamRemoteStorage/GetPublishedFileDetails/v1/")
                .arg(m_server.serverPort()));
    }

    static QByteArray http(int status, const QByteArray& body) {
        return "HTTP/1.1 " + QByteArray::number(status) +
               " Status\r\nContent-Type: application/json\r\nContent-Length: " +
               QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    }

private:
    QTcpServer m_server;
};

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
