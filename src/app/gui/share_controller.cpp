#include "app/gui/share_controller.hpp"

#include "app/core/qt_util.hpp"
#include "app/core/service_runner.hpp"
#include "app/gui/backend.hpp"
#include "app/gui/notification_center.hpp"
#include "core/import_plan.hpp"
#include "core/readiness.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "services/share_service.hpp"
#include "sharing/workshop_id_list.hpp"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSaveFile>
#include <QVariantList>

#include <algorithm>
#include <iterator>
#include <utility>

namespace pmm::app {
namespace {

using sharing::ImportTextFormat;

QString formatKey(ImportTextFormat format) {
    return format == ImportTextFormat::ShareCode ? QStringLiteral("code")
                                                 : QStringLiteral("idList");
}

QString errorTitle(ErrorCode code, ImportTextFormat format) {
    const bool list = format == ImportTextFormat::WorkshopIdList;
    switch (code) {
    case ErrorCode::UnsupportedVersion:
        return QStringLiteral("Unsupported share-code version");
    case ErrorCode::IntegrityCheckFailed:
        return QStringLiteral("The code is damaged");
    case ErrorCode::LimitExceeded:
        return list ? QStringLiteral("The list is too large")
                    : QStringLiteral("The code is too large");
    case ErrorCode::UnsupportedGame:
        return QStringLiteral("Game not supported");
    case ErrorCode::WrongGame:
        return QStringLiteral("Wrong game");
    case ErrorCode::InvalidArgument:
        if (list)
            return QStringLiteral("No game selected");
        [[fallthrough]];
    case ErrorCode::ParseError:
    case ErrorCode::ValidationFailed:
        return list ? QStringLiteral("Not a valid Workshop ID list")
                    : QStringLiteral("Not a valid share code");
    default:
        return list ? QStringLiteral("The list cannot be imported")
                    : QStringLiteral("The code cannot be imported");
    }
}

// `format` is empty when the text was never read (a file that cannot be).
QVariantMap errorPreview(const QString& title, const QString& message, const QString& format = {}) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("errorTitle"), title},
            {QStringLiteral("errorMessage"), message},
            {QStringLiteral("format"), format}};
}

// What Steam said about a missing Workshop entry: "" (not asked or not
// answered), "ok", "notFound", "unavailable", "banned" or "wrongGame".
QString checkKey(const services::ImportPreview& preview, const ImportPlanEntry& entry) {
    if (entry.status != ImportEntryStatus::Missing ||
        entry.entry.identity.source != source::SteamWorkshop)
        return {};
    const auto it = preview.workshop.find(entry.entry.identity.id);
    if (it == preview.workshop.end())
        return {};
    switch (it->second.check) {
    case services::WorkshopItemCheck::Ok:
        return QStringLiteral("ok");
    case services::WorkshopItemCheck::NotFound:
        return QStringLiteral("notFound");
    case services::WorkshopItemCheck::Unavailable:
        return QStringLiteral("unavailable");
    case services::WorkshopItemCheck::Banned:
        return QStringLiteral("banned");
    case services::WorkshopItemCheck::WrongGame:
        return QStringLiteral("wrongGame");
    }
    return {};
}

QString entryStatusText(const ImportPlanEntry& entry, bool downloads, const QString& check) {
    if (check == QLatin1String("notFound"))
        return QStringLiteral("Not found on Workshop");
    if (check == QLatin1String("unavailable"))
        return QStringLiteral("Unavailable on Workshop");
    if (check == QLatin1String("banned"))
        return QStringLiteral("Banned on Workshop");
    if (check == QLatin1String("wrongGame"))
        return QStringLiteral("Another game's mod");
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

// The preview for QML, without the keys of the lookup (generation, lookup,
// lookupText), which the controller adds.
QVariantMap previewMap(const services::ImportPreview& preview, bool downloads) {
    const QString format = formatKey(preview.format);
    if (preview.error)
        return errorPreview(errorTitle(preview.error->code, preview.format),
                            qs(preview.error->message), format);
    if (!preview.manifest || !preview.plan) // both set whenever there is no error
        return errorPreview(errorTitle(ErrorCode::Internal, preview.format),
                            QStringLiteral("The text could not be previewed."), format);
    const bool list = preview.format == ImportTextFormat::WorkshopIdList;
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
        const QString check = checkKey(preview, entry);
        entries.push_back(
            QVariantMap{{QStringLiteral("position"), static_cast<int>(i + 1)},
                        {QStringLiteral("name"), name},
                        {QStringLiteral("source"), qs(entry.entry.identity.source)},
                        {QStringLiteral("providerId"), qs(entry.entry.identity.id)},
                        {QStringLiteral("enabled"), entry.entry.enabled},
                        {QStringLiteral("check"), check},
                        {QStringLiteral("statusText"), entryStatusText(entry, downloads, check)}});
    }
    const ImportCounts& counts = plan.counts;
    const QString listNote =
        list ? QStringLiteral("An ID list does not say which game it is for. It is imported for "
                              "%1, the selected game, with every mod enabled, in list order.")
                   .arg(qs(preview.gameName))
             : QString();
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("errorTitle"), QString()},
            {QStringLiteral("errorMessage"), QString()},
            {QStringLiteral("format"), format},
            {QStringLiteral("listNote"), listNote},
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

// The export dialog's Workshop ID list section.
QVariantMap workshopIdListMap(const Result<sharing::WorkshopIdListExport>& list) {
    if (!list) {
        const QString error = qs(list.error().message);
        return {{QStringLiteral("text"), QString()},
                {QStringLiteral("count"), 0},
                {QStringLiteral("leftOutLocal"), 0},
                {QStringLiteral("leftOutDisabled"), 0},
                {QStringLiteral("leftOutText"), QString()},
                {QStringLiteral("error"), error},
                {QStringLiteral("note"),
                 QStringLiteral("The Workshop ID list could not be made: %1").arg(error)}};
    }
    const auto count = static_cast<qsizetype>(list->ids.size());
    const auto local = static_cast<qsizetype>(list->leftOutLocal);
    const auto disabled = static_cast<qsizetype>(list->leftOutDisabled);
    QStringList leftOut;
    if (local > 0)
        leftOut.push_back(countText(local, "local mod", "local mods"));
    if (disabled > 0)
        leftOut.push_back(countText(disabled, "disabled mod", "disabled mods"));
    const QString leftOutText =
        leftOut.isEmpty()
            ? QString()
            : QStringLiteral("Left out: %1. The share code includes %2.")
                  .arg(leftOut.join(QStringLiteral(" and ")),
                       local + disabled == 1 ? QStringLiteral("it") : QStringLiteral("them"));
    QString note =
        count == 0 ? QStringLiteral("This playset has no enabled Steam Workshop mods, "
                                    "so the list is empty.")
                   : QStringLiteral("%1 in load order. The list does not say which "
                                    "game it is for.")
                         .arg(countText(count, "enabled Workshop mod", "enabled Workshop mods"));
    if (!leftOutText.isEmpty())
        note += QLatin1Char(' ') + leftOutText;
    return {{QStringLiteral("text"), qs(list->text)},
            {QStringLiteral("count"), static_cast<int>(count)},
            {QStringLiteral("leftOutLocal"), static_cast<int>(local)},
            {QStringLiteral("leftOutDisabled"), static_cast<int>(disabled)},
            {QStringLiteral("leftOutText"), leftOutText},
            {QStringLiteral("error"), QString()},
            {QStringLiteral("note"), note}};
}

} // namespace

ShareController::ShareController(Backend* backend, QObject* parent)
    : QObject(parent), m_backend(backend) {
    m_lookupDelay.setSingleShot(true);
    m_lookupDelay.setInterval(600);
    connect(&m_lookupDelay, &QTimer::timeout, this, &ShareController::sendLookup);
    m_lookupDeadline.setSingleShot(true);
    m_lookupDeadline.setInterval(20000);
    connect(&m_lookupDeadline, &QTimer::timeout, this, &ShareController::lookupTimedOut);
}

ShareController::~ShareController() = default;

void ShareController::setLookupTimings(int delayMs, int timeoutMs) {
    m_lookupDelay.setInterval(std::max(0, delayMs));
    m_lookupDeadline.setInterval(std::max(1, timeoutMs));
}

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

void ShareController::setWorkshopIdList(const QVariantMap& list) {
    if (list == m_workshopIdList)
        return;
    m_workshopIdList = list;
    emit workshopIdListChanged();
}

void ShareController::showError(const QString& title, const QString& message) {
    m_backend->notifications()->error(title, message);
}

QString ShareController::exportCode() {
    const std::int64_t playsetId = m_backend->activePlaysetId();
    setLastExportCode({});
    setWorkshopIdList({});
    if (playsetId < 0) {
        m_backend->notifications()->warning(tr("Nothing to export"),
                                            tr("Create or select a playset first."));
        return {};
    }
    const quint64 generation = ++m_exportGeneration;
    setBusy(true);
    // Both in one call; a failure of one does not hide the other.
    struct Exported {
        Result<std::string> code;
        Result<sharing::WorkshopIdListExport> list;
    };
    m_backend->runner().call(
        this,
        [playsetId](AppRuntime& runtime) {
            return Exported{runtime.sharing().exportCode(playsetId),
                            runtime.sharing().exportWorkshopIdList(playsetId)};
        },
        [this, generation](const Exported& result) {
            setBusy(false);
            if (generation != m_exportGeneration)
                return;
            setWorkshopIdList(workshopIdListMap(result.list));
            const Result<std::string>& code = result.code;
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

bool ShareController::copyWorkshopIdList() {
    const QString text = m_workshopIdList.value(QStringLiteral("text")).toString();
    if (text.isEmpty())
        return false;
    if (QClipboard* clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
    m_backend->notifications()->info(
        tr("Workshop ID list copied"),
        tr("It lists %1. Whoever imports it selects the game first.")
            .arg(countText(m_workshopIdList.value(QStringLiteral("count")).toInt(), "Workshop mod",
                           "Workshop mods")));
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
    resetLookup();
    if (trimmed.isEmpty()) {
        clearPreview();
        return false;
    }
    if (trimmed.size() > kMaxCodeFileBytes) {
        const ImportTextFormat format = sharing::detectImportTextFormat(utf8(trimmed.left(8)));
        m_previewMap = errorPreview(
            errorTitle(ErrorCode::LimitExceeded, format),
            tr("Share codes and Workshop ID lists are at most 2 MiB long. This text is much "
               "longer; copy only the code (it starts with PMM1.) or the list."),
            formatKey(format));
        m_previewMap.insert(QStringLiteral("generation"), static_cast<qint64>(generation));
        setLookupKeys(m_previewMap);
        emit previewChanged();
        return false;
    }
    setBusy(true);
    const bool downloads = m_backend->steamEnabled();
    struct Outcome {
        std::shared_ptr<const services::ImportPreview> preview;
        QVariantMap map;
        std::vector<std::string> missing; // Workshop ids to look up
    };
    m_backend->runner().call(
        this,
        // A Workshop ID list is for the game selected in the window.
        [code = utf8(trimmed), game = utf8(m_backend->currentGame()),
         downloads](AppRuntime& runtime) {
            auto preview = std::make_shared<services::ImportPreview>(
                runtime.sharing().previewImport(code, game));
            Outcome outcome;
            outcome.map = previewMap(*preview, downloads);
            outcome.missing = services::workshopIdsToLookUp(*preview);
            outcome.preview = std::move(preview);
            return outcome;
        },
        [this, generation](Outcome outcome) {
            setBusy(false);
            if (generation != m_previewGeneration)
                return; // superseded or cleared
            m_preview = outcome.preview->ok() ? outcome.preview : nullptr;
            m_basePreview = m_preview;
            m_previewMap = std::move(outcome.map);
            m_previewMap.insert(QStringLiteral("generation"), static_cast<qint64>(generation));
            startLookup(std::move(outcome.missing));
            setLookupKeys(m_previewMap);
            emit previewChanged();
        });
    return true;
}

bool ShareController::importFromFile(const QUrl& fileUrl) {
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    QFile file(path);
    const auto fail = [this](const QString& title, const QString& message) {
        const quint64 generation = ++m_previewGeneration;
        m_preview.reset();
        resetLookup();
        m_previewMap = errorPreview(title, message);
        m_previewMap.insert(QStringLiteral("generation"), static_cast<qint64>(generation));
        setLookupKeys(m_previewMap);
        emit previewChanged();
        return false;
    };
    if (!file.open(QIODevice::ReadOnly))
        return fail(tr("Could not read the file"),
                    tr("%1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
    if (file.size() > kMaxCodeFileBytes)
        return fail(QStringLiteral("The text is too large"),
                    tr("%1 is too large to be a share code or Workshop ID list file.")
                        .arg(QFileInfo(path).fileName()));
    return previewImport(QString::fromUtf8(file.read(kMaxCodeFileBytes)));
}

// --- Names of missing Workshop mods -----------------------------------------

void ShareController::resetLookup() {
    m_lookupDelay.stop();
    m_lookupDeadline.stop();
    m_lookup = Lookup::Off;
    m_basePreview.reset();
    m_lookupMissing.clear();
    m_lookupIds.clear();
    m_lookupSkipped = 0;
    m_lookupItems.clear();
}

void ShareController::startLookup(std::vector<std::string> ids) {
    m_lookupMissing = std::move(ids);
    if (!m_basePreview || m_lookupMissing.empty() || m_backend->readOnly() ||
        !m_backend->runner().metadataEnabled())
        return; // Off
    std::vector<std::string> ask;
    for (const std::string& id : m_lookupMissing) {
        const auto cached = m_lookupCache.find(id);
        if (cached != m_lookupCache.end())
            m_lookupItems.push_back(cached->second);
        else
            ask.push_back(id);
    }
    if (ask.size() > kMaxLookupIds) {
        m_lookupSkipped = ask.size() - kMaxLookupIds;
        ask.resize(kMaxLookupIds);
    }
    if (ask.empty()) {
        m_lookup = Lookup::Done;
    } else {
        // Asked once the preview has settled: typing does not send requests.
        m_lookupIds = std::move(ask);
        m_lookup = Lookup::Waiting;
        m_lookupDelay.start();
    }
    if (!m_lookupItems.empty())
        applyLookup(); // the cached answers, at once
}

void ShareController::sendLookup() {
    if (m_lookup != Lookup::Waiting || !m_basePreview || !m_basePreview->manifest)
        return;
    m_lookup = Lookup::Pending;
    m_lookupDeadline.start();
    const quint64 generation = m_previewGeneration;
    m_backend->runner().lookupWorkshopItems(this, m_basePreview->manifest->game, m_lookupIds,
                                            [this, generation](WorkshopLookup lookup) {
                                                lookupAnswered(generation, std::move(lookup));
                                            });
}

void ShareController::lookupAnswered(quint64 generation, WorkshopLookup lookup) {
    // Kept even when the preview has moved on: the next one may ask again.
    if (m_lookupCache.size() + lookup.items.size() > kMaxCachedItems)
        m_lookupCache.clear();
    for (const providers::WorkshopItemMetadata& item : lookup.items)
        m_lookupCache.insert_or_assign(item.id, item);
    if (generation != m_previewGeneration || m_lookup != Lookup::Pending)
        return; // superseded, cleared or past its deadline
    m_lookupDeadline.stop();
    if (!lookup.available || (lookup.items.empty() && lookup.unanswered > 0))
        m_lookup = Lookup::Failed;
    else if (lookup.unanswered > 0 || m_lookupSkipped > 0)
        m_lookup = Lookup::Partial;
    else
        m_lookup = Lookup::Done;
    if (lookup.items.empty()) {
        setLookupKeys(m_previewMap);
        emit previewChanged();
        return;
    }
    m_lookupItems.insert(m_lookupItems.end(), std::make_move_iterator(lookup.items.begin()),
                         std::make_move_iterator(lookup.items.end()));
    applyLookup();
}

void ShareController::lookupTimedOut() {
    if (m_lookup != Lookup::Pending)
        return;
    m_lookup = Lookup::Failed;
    if (m_previewMap.isEmpty())
        return;
    setLookupKeys(m_previewMap);
    emit previewChanged();
}

void ShareController::applyLookup() {
    if (!m_basePreview)
        return;
    const quint64 generation = m_previewGeneration;
    const bool downloads = m_backend->steamEnabled();
    struct Outcome {
        std::shared_ptr<const services::ImportPreview> preview;
        QVariantMap map;
    };
    // Every answer so far, applied to the preview as previewed: calls in
    // flight cannot lose each other's names.
    m_backend->runner().call(
        this,
        [base = m_basePreview, items = m_lookupItems, downloads](AppRuntime&) {
            auto preview = std::make_shared<services::ImportPreview>(
                services::withWorkshopMetadata(*base, items));
            Outcome outcome;
            outcome.map = previewMap(*preview, downloads);
            outcome.preview = std::move(preview);
            return outcome;
        },
        [this, generation](Outcome outcome) {
            if (generation != m_previewGeneration)
                return;
            m_preview = std::move(outcome.preview);
            m_previewMap = std::move(outcome.map);
            m_previewMap.insert(QStringLiteral("generation"), static_cast<qint64>(generation));
            setLookupKeys(m_previewMap);
            emit previewChanged();
        });
}

void ShareController::setLookupKeys(QVariantMap& map) const {
    QString state;
    QString text;
    switch (m_lookup) {
    case Lookup::Off:
        state = QStringLiteral("off");
        if (!m_basePreview || m_lookupMissing.empty())
            break;
        if (m_backend->readOnly())
            text = tr("Missing mods are shown by ID: their names are not looked up on the Steam "
                      "Workshop in read-only mode.");
        else if (!m_backend->runner().metadataEnabled())
            text = tr("Missing mods are shown by ID: their names are not looked up on the Steam "
                      "Workshop while Steam is off in this session.");
        break;
    case Lookup::Waiting:
    case Lookup::Pending:
        state = QStringLiteral("pending");
        text = tr("Looking up %1 on the Steam Workshop…")
                   .arg(countText(static_cast<qsizetype>(m_lookupIds.size()), "missing mod",
                                  "missing mods"));
        break;
    case Lookup::Done:
        state = QStringLiteral("done");
        break;
    case Lookup::Partial:
        state = QStringLiteral("partial");
        text = m_lookupSkipped > 0
                   ? tr("At most %1 missing mods are looked up on the Steam Workshop per preview, "
                        "so %2 shown by ID.")
                         .arg(kMaxLookupIds)
                         .arg(countText(static_cast<qsizetype>(m_lookupSkipped), "is", "are"))
                   : tr("Some missing mods could not be looked up on the Steam Workshop and are "
                        "shown by ID. You can still import.");
        break;
    case Lookup::Failed:
        state = QStringLiteral("failed");
        text = tr("The Steam Workshop could not be reached, so missing mods are shown by ID. You "
                  "can still import.");
        break;
    }
    map.insert(QStringLiteral("lookup"), state);
    map.insert(QStringLiteral("lookupText"), text);
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
    resetLookup();
    if (m_previewMap.isEmpty())
        return;
    m_previewMap.clear();
    emit previewChanged();
}

} // namespace pmm::app
