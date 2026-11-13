#pragma once

// Owns the services worker thread. The AppRuntime, the Workshop session and
// metadata hosts (QProcess, QNetworkAccessManager), the acquisition
// coordinator and its ~1 s tick timer all live on that thread; nothing else
// touches them. Other threads (the GUI) only marshal work to it:
//
//   runner.call(receiver, [](AppRuntime& rt) { return ...; },
//               [](Result r) { /* on the receiver's thread */ });
//
// Calls run in submission order, one at a time. `done` runs queued on the
// receiver's thread - receivers live on the runner's own (GUI) thread - and
// is dropped if the receiver was destroyed meanwhile. Coordinator activity
// that nobody asked for (helper events, metadata, ticks) is announced through
// acquisitionChanged(). lookupWorkshopItems() asks the Web API host for the
// import preview and answers only its caller, never the coordinator.
//
// shutdown() (also run by the destructor) stops the tick, persists progress
// and closes every helper session (coordinator.shutdown(), then the hosts),
// destroys the runtime on the worker and joins the thread.

#include "app/core/app_runtime.hpp"
#include "providers/web_metadata.hpp"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QUrl>

#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace pmm::app {

class RunnerWorker;

// The answer to ServiceRunner::lookupWorkshopItems().
struct WorkshopLookup {
    bool available = false;                             // the session has a Web API host
    std::vector<providers::WorkshopItemMetadata> items; // of the answered batches
    std::size_t unanswered = 0;                         // ids of batches without an answer
};

struct RunnerOptions {
    RuntimeOptions runtime;
    // Workshop downloads through pmm-steam-helper. Off (--no-steam): no
    // coordinator is started and acquisition requests are refused.
    bool steam = true;
    bool metadata = true;     // Web API names / wrong-game checks
    QString helperExecutable; // empty: QtWorkshopSessionHost::defaultHelperPath()
    // Test seams for the Web API host: the endpoint (empty:
    // QtWorkshopMetadataHost::defaultEndpoint()) and the transfer timeout.
    QUrl metadataEndpoint;
    std::chrono::milliseconds metadataTimeout{15000};
    std::chrono::milliseconds tickInterval{1000};
    // Test seam for CoordinatorOptions (content verification, timeouts).
    std::function<void(services::CoordinatorOptions&)> configureCoordinator;
};

struct RunnerStart {
    bool ok = false;
    QString errorMessage; // actionable; shown in the fatal error window
    std::vector<StartupNotice> notices;
    QString dataDir;
    QString logsDir;
    QString cacheDir;
};

class ServiceRunner : public QObject {
    Q_OBJECT

public:
    explicit ServiceRunner(RunnerOptions options, QObject* parent = nullptr);
    ~ServiceRunner() override;

    const RunnerOptions& options() const noexcept { return m_options; }

    // Starts the worker and creates the runtime there; blocks until done.
    RunnerStart start();
    // Idempotent; blocks until the worker thread finished.
    void shutdown();
    bool isRunning() const noexcept { return m_running; }
    bool steamEnabled() const noexcept { return m_steamActive; }
    // Started with Steam and the Web API host: not --no-steam (which
    // --read-only implies), metadata on.
    bool metadataEnabled() const noexcept { return m_metadataActive; }

    // Asks the Web API host about `ids` on the worker, in batches of
    // providers::kMaxPublishedFileDetailsItems, and answers once, when every
    // batch has been answered or has failed: `done` runs queued on the
    // receiver's thread like call()'s, and is dropped if the receiver was
    // destroyed meanwhile. Without a host the answer has `available` false.
    void lookupWorkshopItems(QObject* receiver, GameKey game, std::vector<std::string> ids,
                             std::function<void(WorkshopLookup)> done);

    template <class Fn, class Done> void call(QObject* receiver, Fn fn, Done done) {
        using R = std::invoke_result_t<Fn&, AppRuntime&>;
        Q_ASSERT(receiver && receiver->thread() == thread());
        // Only dereferenced on this object's thread, where the receiver lives.
        QPointer<QObject> guard(receiver);
        dispatch([this, guard, fn = std::move(fn), done = std::move(done)]() mutable {
            AppRuntime* runtime = this->runtime();
            if (!runtime)
                return;
            if constexpr (std::is_void_v<R>) {
                fn(*runtime);
                QMetaObject::invokeMethod(
                    this,
                    [guard, done]() mutable {
                        if (guard)
                            done();
                    },
                    Qt::QueuedConnection);
            } else {
                auto result = std::make_shared<R>(fn(*runtime));
                QMetaObject::invokeMethod(
                    this,
                    [guard, done, result]() mutable {
                        if (guard)
                            done(std::move(*result));
                    },
                    Qt::QueuedConnection);
            }
        });
    }

    // Fire and forget.
    template <class Fn> void post(Fn fn) {
        dispatch([this, fn = std::move(fn)]() mutable {
            if (AppRuntime* runtime = this->runtime())
                fn(*runtime);
        });
    }

    // Waits for the result. Only for start-up and tests: never call it from
    // the worker thread, and avoid it while long work may be queued.
    template <class Fn> auto callBlocking(Fn fn) -> std::invoke_result_t<Fn&, AppRuntime&> {
        using R = std::invoke_result_t<Fn&, AppRuntime&>;
        auto promise = std::make_shared<std::promise<R>>();
        auto future = promise->get_future();
        if (!m_running)
            throw std::logic_error("ServiceRunner::callBlocking() without a running worker");
        dispatch([this, promise, fn = std::move(fn)]() mutable {
            AppRuntime* runtime = this->runtime();
            if (!runtime) {
                promise->set_exception(std::make_exception_ptr(
                    std::logic_error("ServiceRunner::callBlocking() without a runtime")));
                return;
            }
            if constexpr (std::is_void_v<R>) {
                fn(*runtime);
                promise->set_value();
            } else {
                promise->set_value(fn(*runtime));
            }
        });
        return future.get();
    }

    // Reports a coordinator update produced inside a call() (worker thread).
    void report(const services::CoordinatorUpdate& update);

signals:
    // Job states, progress or provider status changed; `rescanned` lists the
    // games whose inventory was rescanned because items were installed.
    void acquisitionChanged(const QStringList& rescanned);
    // A launch is about to create the game process (worker thread emits).
    void processStarting();

private:
    friend class RunnerWorker;
    void dispatch(std::function<void()> task);
    AppRuntime* runtime() const;

    RunnerOptions m_options;
    QThread m_thread;
    RunnerWorker* m_worker = nullptr;
    bool m_running = false;
    bool m_steamActive = false;
    bool m_metadataActive = false;
};

} // namespace pmm::app
