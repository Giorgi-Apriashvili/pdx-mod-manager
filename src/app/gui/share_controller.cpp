#include "app/gui/share_controller.hpp"

#include "app/core/qt_util.hpp"
#include "app/core/service_runner.hpp"
#include "app/gui/backend.hpp"
#include "app/gui/notification_center.hpp"
#include "core/import_plan.hpp"
#include "core/readiness.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "services/share_service.hpp"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSaveFile>
#include <QVariantList>

#include <utility>

namespace pmm::app {
namespace {

QString errorTitle(ErrorCode code) {
    switch (code) {
    case ErrorCode::UnsupportedVersion:
        return QStringLiteral("Unsupported share-code version");
    case ErrorCode::IntegrityCheckFailed:
        return QStringLiteral("The code is damaged");
    case ErrorCode::LimitExceeded:
        return QStringLiteral("The code is too large");
    case ErrorCode::UnsupportedGame:
        return QStringLiteral("Game not supported");
    case ErrorCode::WrongGame:
        return QStringLiteral("Wrong game");
    case ErrorCode::ParseError:
    case ErrorCode::ValidationFailed:
    case ErrorCode::InvalidArgument:
        return QStringLiteral("Not a valid share code");
    default:
        return QStringLiteral("The code cannot be imported");
    }
}

QVariantMap errorPreview(const QString& title, const QString& message) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("errorTitle"), title},
            {QStringLiteral("errorMessage"), message}};
}

QString entryStatusText(const ImportPlanEntry& entry, bool downloads) {
    switch (entry.status) {
    case ImportEntryStatus::Installed:
        return entry.warnings.empty() ? QStringLiteral("Installed")
                                      : QStringLiteral("Installed · see warnings");
    case ImportEntryStatus::Missing:
        return downloads ? QStringLiteral("Missing · will download")
                         : QStringLiteral("Missing · downloads off");
    case ImportEntryStatus::LocalUnmapped:
        return QStringLiteral("Local · needs mapping");
    case ImportEntryStatus::Broken:
        return QStringLiteral("Installed · broken");
    }
    return {};
}

QVariantMap previewMap(const services::ImportPreview& preview, bool downloads) {
    if (preview.error)
        return errorPreview(errorTitle(preview.error->code), qs(preview.error->message));
    if (!preview.manifest || !preview.plan) // both set whenever there is no error
        return errorPreview(errorTitle(ErrorCode::Internal),
                            QStringLiteral("The code could not be previewed."));
    const sharing::Manifest& manifest = *preview.manifest;
    const ImportPlan& plan = *preview.plan;
    QVariantList warnings;
    if (!preview.gameInstalled)
        warnings.push_back(
            QStringLiteral("%1 is not installed or not located on this computer. The playset "
                           "is saved, but nothing can be downloaded or launched until the "
                           "game is found.")
                .arg(qs(preview.gameName)));
    for (const std::string& warning : plan.warnings) {
        if (warning != kIdenticalContentNotGuaranteed) // shown as portabilityNote
            warnings.push_back(qs(warning));
    }
    QVariantList entries;
    entries.reserve(static_cast<qsizetype>(plan.entries.size()));
    for (std::size_t i = 0; i < plan.entries.size(); ++i) {
        const ImportPlanEntry& entry = plan.entries[i];
        QString name = qs(entry.installedName);
        if (name.isEmpty())
            name = qs(entry.entry.nameHint);
        if (name.isEmpty())
            name = qs(entry.entry.identity.id);
        entries.push_back(
            QVariantMap{{QStringLiteral("position"), static_cast<int>(i + 1)},
                        {QStringLiteral("name"), name},
                        {QStringLiteral("source"), qs(entry.entry.identity.source)},
                        {QStringLiteral("providerId"), qs(entry.entry.identity.id)},
                        {QStringLiteral("enabled"), entry.entry.enabled},
                        {QStringLiteral("statusText"), entryStatusText(entry, downloads)}});
    }
    const ImportCounts& counts = plan.counts;
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("errorTitle"), QString()},
            {QStringLiteral("errorMessage"), QString()},
            {QStringLiteral("game"), qs(manifest.game)},
            {QStringLiteral("gameName"), qs(preview.gameName)},
            {QStringLiteral("gameInstalled"), preview.gameInstalled},
            {QStringLiteral("playsetName"), qs(manifest.playsetName)},
            {QStringLiteral("proposedName"), qs(plan.proposedName)},
            {QStringLiteral("total"), static_cast<int>(counts.total)},
            {QStringLiteral("enabled"), static_cast<int>(counts.enabled)},
            {QStringLiteral("installed"), static_cast<int>(counts.installed)},
            {QStringLiteral("missing"), static_cast<int>(counts.missing)},
            {QStringLiteral("missingEnabled"), static_cast<int>(counts.missingEnabled)},
            {QStringLiteral("missingDisabled"), static_cast<int>(counts.missingDisabled)},
            {QStringLiteral("localUnmapped"), static_cast<int>(counts.localUnmapped)},
            {QStringLiteral("downloads"), downloads},
            {QStringLiteral("warnings"), warnings},
            {QStringLiteral("entries"), entries}};
}

// How the missing Workshop entries of an import with downloads off read in
// the mod list. Such an import queues nothing, and the jobs from before it keep
// their state, so each entry reads as its mod's newest job says.
struct UnqueuedEntries {
    int missing = 0; // no job or a cancelled one: Download missing takes them
    int queued = 0;  // an unfinished job, resumed by a session with downloads on
    int failed = 0;  // failed, unavailable or needs action: Retry takes them
};

UnqueuedEntries unqueuedEntries(AppRuntime& runtime, const ImportPlan& plan) {
    UnqueuedEntries out;
    auto jobs = persistence::AcquisitionJobRepository(runtime.database()).listForGame(plan.game);
    if (!jobs) {
        out.missing = static_cast<int>(plan.toAcquire.size());
        return out;
    }
    std::vector<PlaysetEntry> entries(plan.toAcquire.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
        entries[i].identity = plan.toAcquire[i];
    // None of them is installed: that is what put them in toAcquire.
    for (const EntryReadiness& readiness : computePlaysetReadiness(entries, {}, jobs.value())) {
        switch (readiness.status) {
        case EntryStatus::Pending:
        case EntryStatus::Downloading:
            ++out.queued;
            break;
        case EntryStatus::Failed:
        case EntryStatus::Unavailable:
        case EntryStatus::NeedsUserAction:
            ++out.failed;
            break;
        case EntryStatus::Missing:
        case EntryStatus::Ready:
        case EntryStatus::Broken:
            ++out.missing;
            break;
        }
    }
    return out;
}

} // namespace

ShareController::ShareController(Backend* backend, QObject* parent)
    : QObject(parent), m_backend(backend) {}

ShareController::~ShareController() = default;

QString ShareController::portabilityNote() const {
    return tr("A share code fixes which mods are used, their order and which are enabled. It does "
              "not pin mod versions: mods and the game can update independently, so identical "
              "codes do not guarantee identical files or multiplayer checksums.");
}

void ShareController::setBusy(bool busy) {
    const bool before = this->busy();
    m_busy = std::max(0, m_busy + (busy ? 1 : -1));
    if (before != this->busy())
        emit busyChanged();
}

void ShareController::setLastExportCode(const QString& code) {
    if (code == m_lastExportCode)
        return;
    m_lastExportCode = code;
    emit lastExportCodeChanged();
}

void ShareController::showError(const QString& title, const QString& message) {
    m_backend->notifications()->error(title, message);
}

QString ShareController::exportCode() {
    const std::int64_t playsetId = m_backend->activePlaysetId();
    setLastExportCode({});
    if (playsetId < 0) {
        m_backend->notifications()->warning(tr("Nothing to export"),
                                            tr("Create or select a playset first."));
        return {};
    }
    const quint64 generation = ++m_exportGeneration;
    setBusy(true);
    m_backend->runner().call(
        this, [playsetId](AppRuntime& runtime) { return runtime.sharing().exportCode(playsetId); },
        [this, generation](Result<std::string> code) {
            setBusy(false);
            if (generation != m_exportGeneration)
                return;
            if (!code) {
                m_copyWhenExported = false;
                showError(tr("Export failed"), qs(code.error().message));
                emit exported({});
                return;
            }
            setLastExportCode(qs(code.value()));
            emit exported(m_lastExportCode);
            if (std::exchange(m_copyWhenExported, false))
                copyExportCode();
        });
    return m_lastExportCode;
}

bool ShareController::copyExportCode() {
    if (m_lastExportCode.isEmpty()) {
        if (m_backend->activePlaysetId() < 0)
            return false;
        m_copyWhenExported = true;
        if (!busy())
            exportCode();
        return false;
    }
    if (QClipboard* clipboard = QGuiApplication::clipboard())
        clipboard->setText(m_lastExportCode);
    m_backend->notifications()->info(tr("Share code copied"),
                                     tr("Paste it anywhere to share this playset."));
    return true;
}

bool ShareController::exportToFile(const QUrl& fileUrl) {
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty() || m_lastExportCode.isEmpty()) {
        m_backend->notifications()->warning(
            tr("Nothing to save"), tr("Wait until the share code is shown, then save it."));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write((m_lastExportCode + QLatin1Char('\n')).toUtf8()) < 0 || !file.commit()) {
        showError(tr("Could not save the share code"),
                  tr("%1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }
    m_backend->notifications()->info(tr("Share code saved"),
                                     tr("Saved to %1.").arg(QFileInfo(path).fileName()));
    return true;
}

QString ShareController::clipboardText() const {
    const QClipboard* clipboard = QGuiApplication::clipboard();
    return clipboard ? clipboard->text() : QString();
}

bool ShareController::previewImport(const QString& text) {
    const QString trimmed = text.trimmed();
    const quint64 generation = ++m_previewGeneration;
    m_preview.reset();
    if (trimmed.isEmpty()) {
        clearPreview();
        return false;
    }
    if (trimmed.size() > kMaxCodeFileBytes) {
        m_previewMap = errorPreview(errorTitle(ErrorCode::LimitExceeded),
                                    tr("Share codes are at most 2 MiB long. This text is much "
                                       "longer; copy only the code (it starts with PMM1.)."));
        emit previewChanged();
        return false;
    }
    setBusy(true);
    const bool downloads = m_backend->steamEnabled();
    struct Outcome {
        std::shared_ptr<const services::ImportPreview> preview;
        QVariantMap map;
    };
    m_backend->runner().call(
        this,
        [code = utf8(trimmed), downloads](AppRuntime& runtime) {
            auto preview =
                std::make_shared<services::ImportPreview>(runtime.sharing().previewImport(code));
            Outcome outcome;
            outcome.map = previewMap(*preview, downloads);
            outcome.preview = std::move(preview);
            return outcome;
        },
        [this, generation](Outcome outcome) {
            setBusy(false);
            if (generation != m_previewGeneration)
                return; // superseded or cleared
            m_preview = outcome.preview->ok() ? outcome.preview : nullptr;
            m_previewMap = std::move(outcome.map);
            emit previewChanged();
        });
    return true;
}

bool ShareController::importFromFile(const QUrl& fileUrl) {
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        ++m_previewGeneration;
        m_preview.reset();
        m_previewMap =
            errorPreview(tr("Could not read the file"),
                         tr("%1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
        emit previewChanged();
        return false;
    }
    if (file.size() > kMaxCodeFileBytes) {
        ++m_previewGeneration;
        m_preview.reset();
        m_previewMap = errorPreview(
            errorTitle(ErrorCode::LimitExceeded),
            tr("%1 is too large to be a share code file.").arg(QFileInfo(path).fileName()));
        emit previewChanged();
        return false;
    }
    return previewImport(QString::fromUtf8(file.read(kMaxCodeFileBytes)));
}

bool ShareController::confirmImport(const QString& mode, int targetPlaysetId, const QString& name) {
    if (!m_preview || !m_preview->manifest || busy())
        return false;
    const bool replace = mode == QLatin1String("replace");
    const QString game = qs(m_preview->manifest->game);
    if (replace && (game != m_backend->currentGame() || targetPlaysetId < 0)) {
        m_backend->notifications()->warning(
            tr("Choose a playset to replace"),
            tr("A code can only replace a playset of the same game. Import it as a new playset."));
        return false;
    }
    const services::ImportMode importMode =
        replace ? services::ImportMode::Replace : services::ImportMode::New;
    const std::optional<std::int64_t> target =
        replace ? std::optional<std::int64_t>(targetPlaysetId) : std::nullopt;
    // `name` is the new playset's: a replaced playset keeps its own name (the
    // dialog's field still holds the one proposed for a new playset).
    const std::string newName = replace ? std::string() : utf8(name.trimmed());
    const bool steam = m_backend->steamEnabled();
    ServiceRunner* runner = &m_backend->runner();
    struct Outcome {
        std::optional<Error> error;
        std::int64_t playsetId = -1;
        QString name;
        int entries = 0;
        int missing = 0;          // Workshop entries to acquire
        UnqueuedEntries unqueued; // with downloads off
    };
    setBusy(true);
    runner->call(
        this,
        [preview = m_preview, importMode, target, name = newName, steam,
         runner](AppRuntime& runtime) {
            Outcome outcome;
            // With downloads off the import queues nothing; jobs from before
            // it keep their state (see unqueuedEntries()).
            services::ImportOptions options;
            options.queueAcquisition = steam;
            auto applied =
                runtime.sharing().applyImport(*preview, importMode, target, name, options);
            if (!applied) {
                outcome.error = applied.error();
                return outcome;
            }
            outcome.playsetId = applied.value().playsetId;
            outcome.name = qs(applied.value().playsetName);
            outcome.entries = static_cast<int>(applied.value().plan.entries.size());
            outcome.missing = static_cast<int>(applied.value().plan.toAcquire.size());
            if (!steam)
                outcome.unqueued = unqueuedEntries(runtime, applied.value().plan);
            else if (runtime.acquisition())
                runner->report(runtime.acquisition()->start(preview->manifest->game));
            return outcome;
        },
        [this, game, replace, steam](Outcome outcome) {
            setBusy(false);
            if (outcome.error) {
                showError(tr("Import failed"), qs(outcome.error->message));
                emit imported(-1);
                return;
            }
            QString text = replace ? tr("“%1” now has the %2 imported entries. Its previous "
                                        "contents are kept in History.")
                                         .arg(outcome.name)
                                         .arg(outcome.entries)
                                   : tr("“%1” was created with %2 entries.")
                                         .arg(outcome.name)
                                         .arg(outcome.entries);
            const auto missingMods = [](int count) {
                return countText(count, "missing Workshop mod", "missing Workshop mods");
            };
            if (steam && outcome.missing > 0)
                text += QLatin1Char(' ') +
                        tr("%1 queued for download.").arg(missingMods(outcome.missing));
            const UnqueuedEntries& unqueued = outcome.unqueued;
            if (unqueued.missing > 0)
                text += QLatin1Char(' ') +
                        tr("%1 not queued because downloads are off in this session; restart "
                           "Paradox Mod Manager normally, then use Download missing.")
                            .arg(missingMods(unqueued.missing));
            if (unqueued.queued > 0)
                text += QLatin1Char(' ') +
                        tr("%1 still queued from before; downloading resumes when Paradox Mod "
                           "Manager is restarted normally.")
                            .arg(missingMods(unqueued.queued));
            if (unqueued.failed > 0)
                text += QLatin1Char(' ') +
                        tr("%1 could not be downloaded before; restart Paradox Mod Manager "
                           "normally, then use Retry in Downloads.")
                            .arg(missingMods(unqueued.failed));
            m_backend->notifications()->info(tr("Import complete"), text);
            // The stored active playset: the import, or an activation since.
            if (game != m_backend->currentGame())
                m_backend->selectGame(game);
            else
                m_backend->reloadContext();
            emit imported(static_cast<int>(outcome.playsetId));
        });
    return true;
}

void ShareController::clearPreview() {
    ++m_previewGeneration;
    m_preview.reset();
    if (m_previewMap.isEmpty())
        return;
    m_previewMap.clear();
    emit previewChanged();
}

} // namespace pmm::app
