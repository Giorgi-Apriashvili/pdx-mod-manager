#include "app/core/session_host.hpp"

#include "app/core/qt_util.hpp"
#include "core/log.hpp"

#include <windows.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

#include <algorithm>
#include <regex>
#include <string_view>

namespace pmm::app {
namespace {

constexpr const char* kComponent = "app.helper";

// The Steam library's own logging (on the helper's stderr) can contain the
// user's SteamID64.
std::string redactHelperOutput(const std::string& line) {
    static const std::regex steamId("7656119[0-9]{10}");
    return redact(std::regex_replace(line, steamId, "<steamid>"));
}

} // namespace

QtWorkshopSessionHost::QtWorkshopSessionHost(QString helperExecutable, QObject* parent)
    : QObject(parent), m_helper(std::move(helperExecutable)) {}

QtWorkshopSessionHost::~QtWorkshopSessionHost() {
    closeAll();
}

QString QtWorkshopSessionHost::defaultHelperPath() {
    QString overridePath = qEnvironmentVariable("PMM_STEAM_HELPER");
    if (!overridePath.isEmpty())
        return overridePath;
    const QString name = QStringLiteral("pmm-steam-helper.exe");
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

void QtWorkshopSessionHost::setSinks(EventSink events, ExitSink exits) {
    m_events = std::move(events);
    m_exits = std::move(exits);
}

bool QtWorkshopSessionHost::open(const GameKey& gameKey, const std::string& appId,
                                 const std::filesystem::path& steamApiLibrary) {
    close(gameKey);
    // Steam should only ever see one helper per game: let a previous one exit.
    waitForRetired();

    const QFileInfo helper(m_helper);
    if (!helper.isFile()) {
        logWarning(kComponent, "steam helper executable not found",
                   {{"path", utf8(QDir::toNativeSeparators(m_helper))}});
        return false;
    }

    auto session = std::make_unique<Session>();
    session->serial = m_nextSerial++;
    auto* process = new QProcess(this);
    session->process = process;
    process->setProgram(helper.absoluteFilePath());
    process->setArguments({QStringLiteral("--app-id"), qs(appId), QStringLiteral("--steam-api"),
                           qpath(steamApiLibrary)});
    process->setWorkingDirectory(helper.absolutePath());
    process->setProcessChannelMode(QProcess::SeparateChannels);
    // A console program started from a GUI process would get its own window.
    process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });

    const std::uint64_t serial = session->serial;
    const GameKey& game = gameKey; // captured by copy below
    connect(
        process, &QProcess::readyReadStandardOutput, process,
        [this, game, serial] { readOutput(game, serial); }, Qt::QueuedConnection);
    connect(
        process, &QProcess::readyReadStandardError, process,
        [this, game, serial] { readErrors(game, serial); }, Qt::QueuedConnection);
    connect(
        process, &QProcess::finished, process,
        [this, game, serial](int exitCode, QProcess::ExitStatus status) {
            finished(game, serial, status == QProcess::NormalExit ? exitCode : -1);
        },
        Qt::QueuedConnection);
    connect(
        process, &QProcess::errorOccurred, process,
        [this, game, serial](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                finished(game, serial, -1);
        },
        Qt::QueuedConnection);
    m_sessions[gameKey] = std::move(session);

    process->start(QIODevice::ReadWrite);
    if (!process->waitForStarted(5000)) {
        logWarning(kComponent, "steam helper could not be started",
                   {{"game", gameKey}, {"error", utf8(process->errorString())}});
        m_sessions.erase(gameKey);
        retire(process, false);
        return false;
    }
    logInfo(kComponent, "steam helper started",
            {{"game", gameKey}, {"appId", appId}, {"pid", std::to_string(process->processId())}});
    return true;
}

void QtWorkshopSessionHost::send(const GameKey& gameKey, const providers::HelperCommand& command) {
    const auto it = m_sessions.find(gameKey);
    if (it == m_sessions.end() || it->second->process->state() != QProcess::Running) {
        logDebug(kComponent, "command for a closed helper session dropped", {{"game", gameKey}});
        return;
    }
    const std::string line = providers::encodeCommand(command) + "\n";
    it->second->process->write(line.data(), static_cast<qint64>(line.size()));
}

void QtWorkshopSessionHost::close(const GameKey& gameKey) {
    const auto it = m_sessions.find(gameKey);
    if (it == m_sessions.end())
        return;
    QProcess* process = it->second->process;
    m_sessions.erase(it);
    logInfo(kComponent, "closing steam helper", {{"game", gameKey}});
    retire(process, true);
}

bool QtWorkshopSessionHost::isOpen(const GameKey& gameKey) const {
    return m_sessions.count(gameKey) != 0;
}

void QtWorkshopSessionHost::closeAll() {
    while (!m_sessions.empty())
        QtWorkshopSessionHost::close(m_sessions.begin()->first); // also from the destructor
    waitForRetired();
}

QtWorkshopSessionHost::Session* QtWorkshopSessionHost::find(const GameKey& gameKey,
                                                            std::uint64_t serial) {
    const auto it = m_sessions.find(gameKey);
    return it != m_sessions.end() && it->second->serial == serial ? it->second.get() : nullptr;
}

void QtWorkshopSessionHost::readOutput(const GameKey& gameKey, std::uint64_t serial) {
    Session* session = find(gameKey, serial);
    if (!session)
        return;
    const QByteArray bytes = session->process->readAllStandardOutput();
    const std::vector<std::string> lines = session->out.push(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    for (const std::string& line : lines) {
        if (line.empty())
            continue;
        auto event = providers::decodeEvent(line);
        if (!event) {
            logWarning(kComponent, "undecodable helper output skipped",
                       {{"game", gameKey}, {"error", event.error().message}});
            continue;
        }
        if (m_events)
            m_events(gameKey, event.value());
        if (!find(gameKey, serial))
            return; // the coordinator closed the session while handling the event
    }
}

void QtWorkshopSessionHost::readErrors(const GameKey& gameKey, std::uint64_t serial) {
    Session* session = find(gameKey, serial);
    if (!session)
        return;
    const QByteArray bytes = session->process->readAllStandardError();
    for (const std::string& line : session->err.push(
             std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())))) {
        if (!line.empty())
            logDebug(kComponent, "helper stderr",
                     {{"game", gameKey}, {"line", redactHelperOutput(line)}});
    }
}

void QtWorkshopSessionHost::finished(const GameKey& gameKey, std::uint64_t serial, int exitCode) {
    if (!find(gameKey, serial))
        return;
    readOutput(gameKey, serial);
    readErrors(gameKey, serial);
    Session* session = find(gameKey, serial);
    if (!session)
        return;
    QProcess* process = session->process;
    m_sessions.erase(gameKey);
    QObject::disconnect(process, nullptr, nullptr, nullptr);
    process->deleteLater();
    logInfo(kComponent, "steam helper exited",
            {{"game", gameKey}, {"exitCode", std::to_string(exitCode)}});
    if (m_exits)
        m_exits(gameKey, exitCode);
}

void QtWorkshopSessionHost::retire(QProcess* process, bool sendQuit) {
    QObject::disconnect(process, nullptr, nullptr, nullptr);
    if (process->state() == QProcess::NotRunning) {
        process->deleteLater();
        return;
    }
    if (sendQuit && process->state() == QProcess::Running) {
        const std::string line = providers::encodeCommand(providers::QuitCommand{}) + "\n";
        process->write(line.data(), static_cast<qint64>(line.size()));
        process->closeWriteChannel();
    }
    m_retired.push_back(process);
    connect(process, &QProcess::finished, this, [this, process] {
        m_retired.erase(std::remove(m_retired.begin(), m_retired.end(), process), m_retired.end());
        process->deleteLater();
    });
    QTimer::singleShot(m_grace, process, [process] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
}

void QtWorkshopSessionHost::waitForRetired() {
    const std::vector<QProcess*> retired = m_retired;
    for (QProcess* process : retired) {
        if (std::find(m_retired.begin(), m_retired.end(), process) == m_retired.end())
            continue; // finished meanwhile (and scheduled for deletion)
        if (process->state() == QProcess::NotRunning ||
            process->waitForFinished(static_cast<int>(m_grace.count())))
            continue;
        logWarning(kComponent, "steam helper did not exit; terminating it");
        process->kill();
        process->waitForFinished(1000);
    }
}

} // namespace pmm::app
