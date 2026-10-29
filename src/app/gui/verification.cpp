#include "app/gui/verification.hpp"

#include "app/core/qt_util.hpp"
#include "app/gui/app_settings.hpp"
#include "app/gui/backend.hpp"
#include "app/gui/mod_filter_model.hpp"
#include "app/gui/mod_list_model.hpp"
#include "app/gui/share_controller.hpp"
#include "core/log.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTimer>

#include <cstdio>
#include <map>
#include <memory>

namespace pmm::app {
namespace {

void say(const QString& text) {
    std::fprintf(stdout, "[verify] %s\n", text.toUtf8().constData());
    std::fflush(stdout);
    logInfo("app.verify", utf8(text));
}

bool writeText(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

QString readText(const QString& path, bool* ok) {
    QFile file(path);
    *ok = file.open(QIODevice::ReadOnly);
    return *ok ? QString::fromUtf8(file.read(ShareController::kMaxCodeFileBytes)) : QString();
}

} // namespace

Verification::Verification(QQmlApplicationEngine& engine, Backend& backend,
                           VerificationOptions options, QObject* parent)
    : QObject(parent), m_engine(engine), m_backend(backend), m_options(std::move(options)) {}

void Verification::start() {
    m_steps = m_options.selfTest.isEmpty() ? screenshotSteps() : selfTestSteps();
    if (!m_options.selfTest.isEmpty() && !m_options.screenshot.isEmpty()) {
        std::vector<Step> shots = screenshotSteps();
        m_steps.insert(m_steps.end(), shots.begin(), shots.end());
    }
    const auto begin = [this] { QTimer::singleShot(300, this, [this] { runSteps(); }); };
    if (m_backend.initialRefreshDone())
        begin();
    else
        connect(&m_backend, &Backend::initialRefreshFinished, this, begin,
                Qt::SingleShotConnection);
}

void Verification::runSteps() {
    if (m_failed)
        return;
    if (m_next >= m_steps.size()) {
        finish();
        return;
    }
    Step step = m_steps[m_next++];
    step([this] { QTimer::singleShot(0, this, [this] { runSteps(); }); });
}

void Verification::fail(const QString& message) {
    if (m_failed)
        return;
    m_failed = true;
    say(QStringLiteral("FAILED: ") + message);
    QCoreApplication::exit(1);
}

void Verification::finish() {
    say(QStringLiteral("done"));
    QCoreApplication::exit(0);
}

QQuickWindow* Verification::window() const {
    const QList<QObject*> roots = m_engine.rootObjects();
    return roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(roots.first());
}

void Verification::callWindow(const char* method, const QString& argument) {
    if (QQuickWindow* w = window()) {
        const bool ok = argument.isNull()
                            ? QMetaObject::invokeMethod(w, method)
                            : QMetaObject::invokeMethod(w, method, Q_ARG(QString, argument));
        if (!ok)
            fail(QStringLiteral("window function %1 could not be called")
                     .arg(QLatin1String(method)));
    }
}

void Verification::callWindow(const char* method, int argument) {
    if (QQuickWindow* w = window()) {
        if (!QMetaObject::invokeMethod(w, method, Q_ARG(int, argument)))
            fail(QStringLiteral("window function %1 could not be called")
                     .arg(QLatin1String(method)));
    }
}

void Verification::waitFor(std::function<bool()> condition, std::function<void()> then,
                           int timeoutMs, const QString& what) {
    auto* timer = new QTimer(this);
    auto elapsed = std::make_shared<int>(0);
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this,
            [this, timer, elapsed, condition, then, timeoutMs, what] {
                *elapsed += timer->interval();
                if (condition()) {
                    timer->stop();
                    timer->deleteLater();
                    then();
                } else if (*elapsed >= timeoutMs) {
                    timer->stop();
                    timer->deleteLater();
                    fail(QStringLiteral("timed out waiting for %1").arg(what));
                }
            });
    timer->start();
}

std::vector<Verification::Step> Verification::selfTestSteps() {
    std::vector<Step> steps;
    for (const QString& token : m_options.selfTest.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const qsizetype colon = token.indexOf(QLatin1Char(':'));
        const QString name = colon < 0 ? token.trimmed() : token.left(colon).trimmed();
        const QString argument = colon < 0 ? QString() : token.mid(colon + 1).trimmed();

        if (name == QLatin1String("config")) {
            steps.push_back([this](std::function<void()> next) {
                const std::int64_t before = m_backend.activePlaysetId();
                say(QStringLiteral("creating a playset from the current configuration of %1")
                        .arg(m_backend.currentGame()));
                if (!m_backend.importCurrentConfiguration()) {
                    fail(QStringLiteral("no game selected"));
                    return;
                }
                waitFor(
                    [this, before] {
                        return m_backend.activePlaysetId() != before &&
                               m_backend.mods()->playsetId() == m_backend.activePlaysetId();
                    },
                    [this, next] {
                        say(QStringLiteral("playset %1 has %2 entries (%3 enabled)")
                                .arg(m_backend.activePlaysetId())
                                .arg(m_backend.mods()->count())
                                .arg(m_backend.mods()->enabledCount()));
                        next();
                    },
                    30000, QStringLiteral("the configuration import"));
            });
        } else if (name == QLatin1String("disable") || name == QLatin1String("move")) {
            // Edits through the model, like the UI: "disable:<row>", "move:<from>:<to>".
            steps.push_back([this, name, argument](std::function<void()> next) {
                ModListModel* mods = m_backend.mods();
                const QStringList parts = argument.split(QLatin1Char(':'));
                bool ok = !parts.isEmpty();
                const int first = parts.value(0).toInt(&ok);
                if (name == QLatin1String("disable") && ok && first < mods->count()) {
                    mods->setEnabled(first, false);
                } else if (name == QLatin1String("move") && ok && parts.size() == 2 &&
                           first < mods->count() && parts[1].toInt() < mods->count()) {
                    mods->moveRow(first, parts[1].toInt());
                } else {
                    fail(QStringLiteral("invalid step %1:%2").arg(name, argument));
                    return;
                }
                waitFor([mods] { return !mods->hasPendingEdits(); },
                        [this, name, argument, next] {
                            say(QStringLiteral("%1 %2 saved").arg(name, argument));
                            next();
                        },
                        30000, QStringLiteral("the edit to be saved"));
            });
        } else if (name == QLatin1String("export")) {
            steps.push_back([this, argument](std::function<void()> next) {
                ShareController* sharing = m_backend.sharing();
                connect(
                    sharing, &ShareController::exported, this,
                    [this, argument, next](const QString& code) {
                        if (code.isEmpty()) {
                            fail(QStringLiteral("export failed"));
                            return;
                        }
                        if (!writeText(argument, (code + QLatin1Char('\n')).toUtf8())) {
                            fail(QStringLiteral("could not write %1").arg(argument));
                            return;
                        }
                        say(QStringLiteral("exported %1 characters to %2")
                                .arg(code.size())
                                .arg(argument));
                        next();
                    },
                    Qt::SingleShotConnection);
                sharing->exportCode();
            });
        } else if (name == QLatin1String("import")) {
            steps.push_back([this, argument](std::function<void()> next) {
                bool ok = false;
                const QString code = readText(argument, &ok);
                if (!ok) {
                    fail(QStringLiteral("could not read %1").arg(argument));
                    return;
                }
                ShareController* sharing = m_backend.sharing();
                sharing->previewImport(code);
                waitFor(
                    [sharing] { return sharing->hasPreview() && !sharing->busy(); },
                    [this, sharing, next] {
                        const QVariantMap preview = sharing->preview();
                        if (!preview.value(QStringLiteral("ok")).toBool()) {
                            fail(
                                QStringLiteral("preview rejected the code: %1 %2")
                                    .arg(preview.value(QStringLiteral("errorTitle")).toString(),
                                         preview.value(QStringLiteral("errorMessage")).toString()));
                            return;
                        }
                        say(QStringLiteral(
                                "preview: %1 entries, %2 enabled, %3 installed, %4 missing")
                                .arg(preview.value(QStringLiteral("total")).toInt())
                                .arg(preview.value(QStringLiteral("enabled")).toInt())
                                .arg(preview.value(QStringLiteral("installed")).toInt())
                                .arg(preview.value(QStringLiteral("missing")).toInt()));
                        const QString game = preview.value(QStringLiteral("game")).toString();
                        connect(
                            sharing, &ShareController::imported, this,
                            [this, game, next](int playsetId) {
                                if (playsetId < 0) {
                                    fail(QStringLiteral("the import was not applied"));
                                    return;
                                }
                                waitFor(
                                    [this, game, playsetId] {
                                        return m_backend.currentGame() == game &&
                                               m_backend.activePlaysetId() == playsetId &&
                                               m_backend.mods()->playsetId() == playsetId &&
                                               !m_backend.mods()->hasPendingEdits();
                                    },
                                    [this, playsetId, next] {
                                        say(QStringLiteral("imported as playset %1 with %2 entries")
                                                .arg(playsetId)
                                                .arg(m_backend.mods()->count()));
                                        next();
                                    },
                                    30000, QStringLiteral("the imported playset"));
                            },
                            Qt::SingleShotConnection);
                        if (!sharing->confirmImport(
                                QStringLiteral("new"), -1,
                                preview.value(QStringLiteral("proposedName")).toString()))
                            fail(QStringLiteral("confirmImport was refused"));
                    },
                    30000, QStringLiteral("the import preview"));
            });
        } else if (name == QLatin1String("dump")) {
            steps.push_back([this, argument](std::function<void()> next) {
                QJsonArray entries;
                for (const ModRowData& row : m_backend.mods()->rows()) {
                    entries.push_back(QJsonObject{{QStringLiteral("source"), row.source},
                                                  {QStringLiteral("id"), row.providerId},
                                                  {QStringLiteral("enabled"), row.entry.enabled}});
                }
                const QJsonObject root{{QStringLiteral("game"), m_backend.currentGame()},
                                       {QStringLiteral("entries"), entries}};
                if (!writeText(argument, QJsonDocument(root).toJson(QJsonDocument::Indented))) {
                    fail(QStringLiteral("could not write %1").arg(argument));
                    return;
                }
                say(QStringLiteral("wrote %1 entries to %2").arg(entries.size()).arg(argument));
                next();
            });
        } else {
            steps.push_back([this, token](std::function<void()>) {
                fail(QStringLiteral("unknown self-test step \"%1\"").arg(token));
            });
        }
    }
    return steps;
}

std::vector<Verification::Step> Verification::screenshotSteps() {
    std::vector<Step> steps;
    if (m_options.screenshot.isEmpty())
        return steps;
    steps.push_back([this](std::function<void()> next) {
        if (!m_options.theme.isEmpty())
            m_backend.settings()->setTheme(m_options.theme);
        if (!m_options.statusFilter.isEmpty())
            m_backend.modView()->setStatusFilter(m_options.statusFilter);
        if (!m_options.search.isEmpty())
            m_backend.modView()->setSearch(m_options.search);
        if (m_options.selectRow >= 0)
            callWindow("selectMod", m_options.selectRow);
        next();
    });
    for (const QString& item : m_options.open) {
        if (item.startsWith(QLatin1String("import:"))) {
            const QString path = item.mid(7);
            steps.push_back([this, path](std::function<void()> next) {
                bool ok = false;
                const QString code = readText(path, &ok);
                if (!ok) {
                    fail(QStringLiteral("could not read %1").arg(path));
                    return;
                }
                callWindow("openImport", code.trimmed());
                ShareController* sharing = m_backend.sharing();
                waitFor([sharing] { return sharing->hasPreview() && !sharing->busy(); }, next,
                        30000, QStringLiteral("the import preview"));
            });
        } else if (item == QLatin1String("export")) {
            steps.push_back([this](std::function<void()> next) {
                callWindow("openExport");
                ShareController* sharing = m_backend.sharing();
                waitFor([sharing] { return !sharing->lastExportCode().isEmpty(); }, next, 30000,
                        QStringLiteral("the export"));
            });
        } else {
            static const std::map<QString, const char*> dialogs{
                {QStringLiteral("add-mods"), "openAddMods"},
                {QStringLiteral("settings"), "openSettings"},
                {QStringLiteral("history"), "openHistory"},
                {QStringLiteral("locate"), "openLocate"}};
            const auto it = dialogs.find(item);
            if (it == dialogs.end()) {
                steps.push_back([this, item](std::function<void()>) {
                    fail(QStringLiteral("unknown --open value \"%1\"").arg(item));
                });
                continue;
            }
            const char* method = it->second;
            steps.push_back([this, method](std::function<void()> next) {
                callWindow(method);
                next();
            });
        }
    }
    steps.push_back([this](std::function<void()> next) {
        QTimer::singleShot(m_options.screenshotDelayMs, this, [this, next] {
            QQuickWindow* w = window();
            if (!w) {
                fail(QStringLiteral("no window to capture"));
                return;
            }
            const QImage image = w->grabWindow();
            if (image.isNull() || !image.save(m_options.screenshot, "PNG")) {
                fail(QStringLiteral("could not write %1").arg(m_options.screenshot));
                return;
            }
            say(QStringLiteral("screenshot %1 (%2x%3)")
                    .arg(m_options.screenshot)
                    .arg(image.width())
                    .arg(image.height()));
            next();
        });
    });
    return steps;
}

} // namespace pmm::app
