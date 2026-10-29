#include "app/gui/launch_controller.hpp"

#include "app/core/qt_util.hpp"
#include "app/core/service_runner.hpp"
#include "app/gui/backend.hpp"
#include "app/gui/games_model.hpp"
#include "app/gui/notification_center.hpp"
#include "core/catalog.hpp"
#include "services/diagnostics.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>

namespace pmm::app {

LaunchController::LaunchController(Backend* backend, QObject* parent)
    : QObject(parent), m_backend(backend) {}

void LaunchController::apply(LaunchData data) {
    m_data = std::move(data);
    emit changed();
}

void LaunchController::setState(const QString& state, bool busy) {
    const bool playChanged = busy != m_busy;
    if (state == m_state && !playChanged)
        return;
    m_state = state;
    m_busy = busy;
    emit stateChanged();
    if (playChanged)
        emit changed(); // canPlay depends on busy
}

void LaunchController::processStarting() {
    if (m_busy)
        setState(QStringLiteral("starting"), true);
}

void LaunchController::play() {
    if (m_busy)
        return;
    if (m_backend->readOnly()) {
        m_backend->notifications()->warning(
            tr("Launching is disabled"),
            tr("This session runs in read-only verification mode: game configuration is never "
               "written and no game is started."));
        return;
    }
    if (!m_data.canPlay) {
        m_backend->notifications()->warning(tr("The game cannot be started yet"),
                                            m_data.blockers.join(QLatin1Char('\n')));
        return;
    }
    const QString game = m_backend->currentGame();
    const std::int64_t playsetId = m_backend->activePlaysetId();
    const QueryOptions options = m_backend->queryOptions();
    m_lastError.clear();
    m_lastErrorDetail.clear();
    setState(QStringLiteral("validating"), true);

    struct Outcome {
        std::optional<Error> error;
        std::int64_t pid = 0;
        QStringList warnings;
        LaunchData launch;
    };
    ServiceRunner* runner = &m_backend->runner();
    QPointer<LaunchController> self(this); // dereferenced on the GUI thread only
    runner->call(
        this,
        [runner, self, game, playsetId, options](AppRuntime& runtime) {
            Outcome outcome;
            const GameKey key = utf8(game);
            const services::LaunchCheck check = runtime.launcher().check(key, playsetId);
            if (!check.canLaunch) {
                std::string text;
                for (const std::string& blocker : check.blockers)
                    text += (text.empty() ? "" : "\n") + blocker;
                outcome.error = makeError(ErrorCode::ValidationFailed, text);
            } else {
                QMetaObject::invokeMethod(
                    runner,
                    [self] {
                        if (self && self->m_state == QLatin1String("validating"))
                            self->setState(QStringLiteral("applying"), true);
                    },
                    Qt::QueuedConnection);
                auto launched = runtime.launcher().launch(key, playsetId);
                if (!launched) {
                    outcome.error = launched.error();
                } else {
                    outcome.pid = launched.value().pid;
                    outcome.warnings = qlist(launched.value().warnings);
                }
            }
            outcome.launch = queries::launchData(runtime, game, playsetId, options);
            return outcome;
        },
        [this, game](Outcome outcome) {
            apply(std::move(outcome.launch));
            const QString gameName = m_backend->games()->currentName();
            if (outcome.error) {
                m_lastError = qs(outcome.error->message);
                m_lastErrorDetail = qs(outcome.error->detail);
                setState(QStringLiteral("failed"), false);
                m_backend->notifications()->error(tr("Could not start %1").arg(gameName),
                                                  m_lastError);
                emit launchFailed(m_lastError);
                return;
            }
            setState(QStringLiteral("started"), false);
            m_backend->notifications()->info(
                tr("%1 started").arg(gameName),
                outcome.warnings.isEmpty()
                    ? tr("The game was started directly with this playset, without the Paradox "
                         "launcher.")
                    : outcome.warnings.join(QLatin1Char('\n')));
            emit launched(static_cast<int>(outcome.pid));
            // The running game appears as a blocker once its process shows up.
            QTimer::singleShot(4000, this, [this] { m_backend->refreshLaunch(); });
            QTimer::singleShot(15000, this, [this] { m_backend->refreshLaunch(); });
        });
}

bool LaunchController::recoverInterrupted() {
    if (m_backend->readOnly()) {
        m_backend->notifications()->warning(
            tr("Recovery is disabled"),
            tr("This session runs in read-only verification mode; restart normally to recover."));
        return false;
    }
    struct Outcome {
        int recovered = 0;
        QStringList errors;
    };
    m_backend->runner().call(
        this,
        [](AppRuntime& runtime) {
            Outcome outcome;
            auto interrupted = runtime.launcher().interruptedTransactions();
            if (!interrupted) {
                outcome.errors.push_back(qs(interrupted.error().message));
                return outcome;
            }
            for (const platform::TransactionRecord& record : interrupted.value()) {
                auto recovered = runtime.launcher().recoverInterrupted(record.id);
                if (recovered)
                    ++outcome.recovered;
                else
                    outcome.errors.push_back(qs(recovered.error().message));
            }
            return outcome;
        },
        [this](Outcome outcome) {
            if (!outcome.errors.isEmpty())
                m_backend->notifications()->error(tr("Recovery incomplete"),
                                                  outcome.errors.join(QLatin1Char('\n')));
            else
                m_backend->notifications()->info(
                    tr("Configuration recovered"),
                    tr("The game configuration files were put back as they were before the "
                       "interrupted launch."));
            m_backend->refreshLaunch();
        });
    return true;
}

bool LaunchController::restoreLastBackup() {
    if (m_backend->readOnly() || !m_data.canRestoreBackup)
        return false;
    const QString game = m_backend->currentGame();
    m_backend->runner().call(
        this,
        [game](AppRuntime& runtime) -> std::optional<Error> {
            auto last = runtime.launcher().lastRestorableTransaction(utf8(game));
            if (!last)
                return last.error();
            if (!last.value())
                return makeError(ErrorCode::NotFound,
                                 "There is no configuration from before a launch to restore.");
            auto restored = runtime.launcher().restoreTransaction(last.value()->id);
            if (!restored)
                return restored.error();
            return std::nullopt;
        },
        [this](std::optional<Error> error) {
            if (error)
                m_backend->notifications()->error(tr("Could not restore the configuration"),
                                                  qs(error->message));
            else
                m_backend->notifications()->info(
                    tr("Configuration restored"),
                    tr("The game configuration files are back as they were before the last launch "
                       "from this application."));
            m_backend->refreshLaunch();
        });
    return true;
}

void LaunchController::copyDiagnostics() {
    m_backend->runner().call(
        this,
        [](AppRuntime& runtime) {
            services::DiagnosticsOptions options;
            options.appVersion = PMM_APP_VERSION;
            if (const services::AcquisitionCoordinator* coordinator = runtime.acquisition()) {
                for (const GameCatalogEntry& entry : gameCatalog()) {
                    if (entry.adapterAvailable)
                        options.acquisition.push_back(coordinator->snapshot(GameKey(entry.key)));
                }
            }
            return qs(services::diagnosticsText(runtime.context(), options));
        },
        [this](const QString& text) {
            m_diagnostics = text;
            if (QClipboard* clipboard = QGuiApplication::clipboard())
                clipboard->setText(text);
            m_backend->notifications()->info(
                tr("Diagnostics copied"),
                tr("The report is on the clipboard. User names and personal paths are redacted."));
            emit diagnosticsReady(text);
        });
}

} // namespace pmm::app
