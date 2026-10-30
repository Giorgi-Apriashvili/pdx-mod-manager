#include "app/core/service_runner.hpp"

#include "app/core/metadata_host.hpp"
#include "app/core/qt_util.hpp"
#include "app/core/session_host.hpp"
#include "core/log.hpp"

#include <QTimer>

namespace pmm::app {

// Lives on the worker thread and owns everything that runs there.
class RunnerWorker : public QObject {
public:
    explicit RunnerWorker(ServiceRunner& runner) : m_runner(runner) {}

    RunnerStart startUp() {
        const RunnerOptions& options = m_runner.m_options;
        RuntimeHosts hosts;
        if (options.steam) {
            m_sessions = std::make_unique<QtWorkshopSessionHost>(
                options.helperExecutable.isEmpty() ? QtWorkshopSessionHost::defaultHelperPath()
                                                   : options.helperExecutable);
            if (options.metadata)
                m_metadata = std::make_unique<QtWorkshopMetadataHost>();
            hosts.sessions = m_sessions.get();
            hosts.metadata = m_metadata.get();
            hosts.coordinator.helperExecutable = toPath(m_sessions->helperExecutable());
            if (options.configureCoordinator)
                options.configureCoordinator(hosts.coordinator);
        }

        RunnerStart start;
        auto created = AppRuntime::create(options.runtime, hosts);
        if (!created) {
            m_metadata.reset();
            m_sessions.reset();
            start.errorMessage = qs(created.error().message);
            return start;
        }
        m_runtime = std::move(created).value();

        if (services::AcquisitionCoordinator* coordinator = m_runtime->acquisition()) {
            m_sessions->setSinks(
                [this, coordinator](const GameKey& game, const providers::HelperEvent& event) {
                    m_runner.report(coordinator->onHelperEvent(game, event));
                },
                [this, coordinator](const GameKey& game, int exitCode) {
                    m_runner.report(coordinator->onHelperExited(game, exitCode));
                });
            if (m_metadata) {
                m_metadata->setSink(
                    [this, coordinator](const GameKey& game,
                                        const std::vector<providers::WorkshopItemMetadata>& items) {
                        m_runner.report(coordinator->onMetadata(game, items));
                    });
            }
            m_tick = new QTimer(this);
            m_tick->setInterval(options.tickInterval);
            QObject::connect(m_tick, &QTimer::timeout, this, [this, coordinator] {
                m_runner.report(coordinator->tick(m_runtime->context().now()));
            });
            m_tick->start();
        }
        m_runtime->setBeforeProcessStart([this] { emit m_runner.processStarting(); });

        start.ok = true;
        start.notices = m_runtime->notices();
        start.dataDir = qpath(m_runtime->paths().dataDir);
        start.logsDir = qpath(m_runtime->paths().logsDir);
        start.cacheDir = qpath(m_runtime->paths().cacheDir);
        return start;
    }

    void tearDown() {
        if (m_tick) {
            m_tick->stop();
            delete m_tick;
            m_tick = nullptr;
        }
        if (m_runtime)
            m_runtime->shutdown();
        if (m_sessions)
            m_sessions->closeAll();
        m_runtime.reset(); // before the hosts the coordinator refers to
        m_metadata.reset();
        m_sessions.reset();
    }

    AppRuntime* runtime() const noexcept { return m_runtime.get(); }

private:
    ServiceRunner& m_runner;
    std::unique_ptr<QtWorkshopSessionHost> m_sessions;
    std::unique_ptr<QtWorkshopMetadataHost> m_metadata;
    std::unique_ptr<AppRuntime> m_runtime;
    QTimer* m_tick = nullptr;
};

ServiceRunner::ServiceRunner(RunnerOptions options, QObject* parent)
    : QObject(parent), m_options(std::move(options)) {
    m_thread.setObjectName(QStringLiteral("pmm-services"));
}

ServiceRunner::~ServiceRunner() {
    shutdown();
}

RunnerStart ServiceRunner::start() {
    if (m_thread.isRunning())
        return {false, QStringLiteral("The services worker is already running."), {}, {}, {}, {}};
    m_worker = new RunnerWorker(*this);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
    m_running = true;

    RunnerStart result;
    QMetaObject::invokeMethod(
        m_worker, [this, &result] { result = m_worker->startUp(); }, Qt::BlockingQueuedConnection);
    m_steamActive = result.ok && m_options.steam;
    return result;
}

void ServiceRunner::shutdown() {
    if (!m_thread.isRunning())
        return;
    QMetaObject::invokeMethod(
        m_worker, [this] { m_worker->tearDown(); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
    m_worker = nullptr; // deleted on the worker when the thread finished
    m_running = false;
    m_steamActive = false;
}

void ServiceRunner::dispatch(std::function<void()> task) {
    if (!m_worker || !m_running)
        return;
    QMetaObject::invokeMethod(m_worker, std::move(task), Qt::QueuedConnection);
}

AppRuntime* ServiceRunner::runtime() const {
    return m_worker ? m_worker->runtime() : nullptr;
}

void ServiceRunner::report(const services::CoordinatorUpdate& update) {
    if (!update.changed && update.rescanned.empty())
        return;
    QStringList rescanned;
    for (const services::GameState& state : update.rescanned)
        rescanned.push_back(qs(state.key));
    emit acquisitionChanged(rescanned);
}

} // namespace pmm::app
