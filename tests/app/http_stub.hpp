#pragma once

// A local HTTP endpoint for the Steam Web API tests (QTcpServer on
// 127.0.0.1): it records every request and answers with a canned response, a
// response built from the request body, or not at all, and it can hold
// requests until release(). Nothing leaves the machine.

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pmm::app::test {

class HttpStub : public QObject {
public:
    // Full HTTP response; nullopt (and no responder): requests are never answered.
    std::optional<QByteArray> response;
    // Builds the full HTTP response from a request body; takes precedence.
    std::function<QByteArray(const QByteArray& body)> responder;
    QByteArray lastRequest;   // headers and body
    QList<QByteArray> bodies; // every request body, in arrival order
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
                    const QByteArray body = buffer->mid(headerEnd + 4, length);
                    bodies.push_back(body);
                    ++requests;
                    if (m_held)
                        m_waiting.push_back({socket, body});
                    else
                        answer(socket, body);
                });
            }
        });
    }

    QUrl url() const {
        return QUrl(
            QStringLiteral("http://127.0.0.1:%1/ISteamRemoteStorage/GetPublishedFileDetails/v1/")
                .arg(m_server.serverPort()));
    }

    // Requests arriving from now on wait for release().
    void hold() { m_held = true; }
    // Answers the waiting requests, in arrival order, and the later ones at once.
    void release() {
        m_held = false;
        auto waiting = std::exchange(m_waiting, {});
        for (auto& [socket, body] : waiting) {
            if (socket)
                answer(socket, body);
        }
    }
    int waiting() const { return static_cast<int>(m_waiting.size()); }

    static QByteArray http(int status, const QByteArray& body) {
        return "HTTP/1.1 " + QByteArray::number(status) +
               " Status\r\nContent-Type: application/json\r\nContent-Length: " +
               QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    }

    // The item ids of a GetPublishedFileDetails request body, in order.
    static std::vector<std::string> requestedIds(const QByteArray& body) {
        std::vector<std::string> ids;
        for (const QByteArray& pair : body.split('&')) {
            if (pair.startsWith("publishedfileids%5B")) {
                const qsizetype equals = pair.indexOf('=');
                ids.push_back(pair.mid(equals + 1).toStdString());
            }
        }
        return ids;
    }

    struct Item {
        std::string title; // empty: not found (result 9)
        std::string appId = "281990";
    };
    // A reply for the requested ids: the known ones found with their title
    // and app, the others not found.
    static std::function<QByteArray(const QByteArray&)> catalog(std::map<std::string, Item> items) {
        return [items = std::move(items)](const QByteArray& body) {
            const auto escaped = [](const std::string& text) {
                return QByteArray::fromStdString(text).replace("\\", "\\\\").replace("\"", "\\\"");
            };
            QByteArray details;
            for (const std::string& id : requestedIds(body)) {
                if (!details.isEmpty())
                    details += ',';
                const auto it = items.find(id);
                if (it == items.end() || it->second.title.empty()) {
                    details += R"({"publishedfileid":")" + QByteArray::fromStdString(id) +
                               R"(","result":9})";
                    continue;
                }
                details += R"({"publishedfileid":")" + QByteArray::fromStdString(id) +
                           R"(","result":1,"consumer_app_id":)" +
                           QByteArray::fromStdString(it->second.appId) + R"(,"title":")" +
                           escaped(it->second.title) + R"(","banned":0,"visibility":0})";
            }
            return http(200,
                        R"({"response":{"result":1,"publishedfiledetails":[)" + details + "]}}");
        };
    }

private:
    void answer(QTcpSocket* socket, const QByteArray& body) {
        if (responder) {
            socket->write(responder(body));
            socket->disconnectFromHost();
        } else if (response) {
            socket->write(*response);
            socket->disconnectFromHost();
        }
    }

    QTcpServer m_server;
    bool m_held = false;
    std::vector<std::pair<QPointer<QTcpSocket>, QByteArray>> m_waiting;
};

} // namespace pmm::app::test
