#pragma once

// A local stand-in for GitHub's API and download hosts (QTcpServer, plain HTTP
// on 127.0.0.1): canned responses per path, or none at all. Nothing leaves the
// machine.

#include "app/core/qt_util.hpp"
#include "core/sha256.hpp"
#include "services/app_update.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace pmm::app::test {

class ReleaseStub : public QObject {
public:
    enum class Mode {
        Complete,  // headers and body with Content-Length
        NoLength,  // no Content-Length: the body ends when the connection closes
        NoAnswer,  // accepts the request and stays silent
        StallHalf, // announces the body, sends half of it and stays silent
    };

    struct Route {
        int status = 200;
        QByteArray body;
        Mode mode = Mode::Complete;
        QByteArray location;
    };

    std::map<QString, Route> routes;

    ReleaseStub() {
        REQUIRE(m_server.listen(QHostAddress::LocalHost, 0));
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                ++m_connections;
                auto buffer = std::make_shared<QByteArray>();
                QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    if (buffer->contains("\r\n\r\n"))
                        answer(socket, *buffer);
                });
            }
        });
    }

    int connections() const { return m_connections; }
    const std::vector<QByteArray>& requests() const { return m_requests; }
    int requestsFor(const QString& path) const {
        int count = 0;
        for (const QByteArray& request : m_requests) {
            if (request.startsWith("GET " + path.toUtf8() + " "))
                ++count;
        }
        return count;
    }
    quint16 port() const { return m_server.serverPort(); }

    services::UpdateEndpoints endpoints() const {
        services::UpdateEndpoints out;
        out.scheme = "http";
        out.apiHost = "127.0.0.1";
        out.downloadHost = "127.0.0.1";
        out.redirectHosts = {"127.0.0.1"};
        out.port = port();
        return out;
    }

    static QString repo() { return qs(services::updateRepository()); }
    static QString apiPath() { return "/repos/" + repo() + "/releases/latest"; }
    static QString assetPath(const std::string& version, const std::string& name) {
        return "/" + repo() + "/releases/download/v" + qs(version) + "/" + qs(name);
    }
    QByteArray absolute(const QString& path, const QString& host = "127.0.0.1") const {
        return QStringLiteral("http://%1:%2%3").arg(host).arg(port()).arg(path).toUtf8();
    }

    static std::string installerName(const std::string& version) {
        return "ParadoxModManager-" + version + "-windows-x86_64-setup.exe";
    }
    static QString installerPath(const std::string& version) {
        return assetPath(version, installerName(version));
    }
    static QString checksumPath(const std::string& version) {
        return assetPath(version, "SHA256SUMS.txt");
    }

    // The reply of the releases/latest endpoint for a release with both assets.
    nlohmann::json releaseJson(const std::string& version) const {
        const auto asset = [&](const QString& path, const std::string& name) {
            return nlohmann::json{{"name", name},
                                  {"browser_download_url", utf8(QString::fromUtf8(absolute(path)))},
                                  {"size", 1}};
        };
        return {
            {"tag_name", "v" + version},
            {"draft", false},
            {"prerelease", false},
            {"html_url", "https://evil.example/release"},
            {"body", "<img src=\"https://evil.example/notes.png\">"},
            {"assets", nlohmann::json::array({asset(installerPath(version), installerName(version)),
                                              asset(checksumPath(version), "SHA256SUMS.txt")})}};
    }

    // Serves a complete release: the API reply, the checksum file and the
    // installer. Tests then replace the route they are about.
    void serveRelease(const std::string& version, const QByteArray& installer) {
        routes[apiPath()].body = QByteArray::fromStdString(releaseJson(version).dump());
        const std::span<const std::uint8_t> bytes(
            reinterpret_cast<const std::uint8_t*>(installer.constData()),
            static_cast<std::size_t>(installer.size()));
        routes[checksumPath(version)].body =
            QByteArray::fromStdString(sha256Hex(bytes) + "  " + installerName(version) + "\n");
        routes[installerPath(version)].body = installer;
    }

private:
    void answer(QTcpSocket* socket, const QByteArray& request) {
        m_requests.push_back(request);
        const QString path = QString::fromUtf8(request.split(' ').value(1));
        const auto route = routes.find(path);
        if (route == routes.end()) {
            socket->write(
                "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        const Route& r = route->second;
        if (r.mode == Mode::NoAnswer)
            return;
        QByteArray head = "HTTP/1.1 " + QByteArray::number(r.status) + " Status\r\n";
        if (!r.location.isEmpty())
            head += "Location: " + r.location + "\r\n";
        if (r.mode != Mode::NoLength)
            head += "Content-Length: " + QByteArray::number(r.body.size()) + "\r\n";
        head += "Connection: close\r\n\r\n";
        socket->write(head);
        if (r.mode == Mode::StallHalf) {
            socket->write(r.body.left(r.body.size() / 2));
            return;
        }
        socket->write(r.body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    int m_connections = 0;
    std::vector<QByteArray> m_requests;
};

} // namespace pmm::app::test
