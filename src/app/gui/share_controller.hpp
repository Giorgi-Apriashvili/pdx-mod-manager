#pragma once

// Share-code and Workshop ID list export and import (docs/ui-contract.md).
// Exports and previews run on the services worker: exportCode() starts an
// export and the code arrives in lastExportCode, the active playset's Workshop
// ID list in workshopIdList; previewImport() validates a share code or a
// Workshop ID list (for the current game) without side effects and fills
// `preview`; confirmImport() applies the previewed text (new playset by
// default, or an explicit replace, which keeps the replaced playset's name;
// `name` is only the new playset's) and queues acquisition of missing Workshop
// entries. A code for another supported game is imported as a new playset of
// that game, and the window switches to it.
//
// Names of missing Workshop mods: once a preview has settled (a short delay,
// so typing does not send requests), the Steam Web API is asked for the
// missing Workshop entries' titles and verdicts (at most kMaxLookupIds per
// preview), and the preview is updated with the same `generation`. Answers are
// cached for the session. Never in read-only sessions or without the Web API
// host (--no-steam); a stale answer is dropped, and a lookup that takes longer
// than its deadline ends "failed". `busy` does not include the lookup: a
// preview can be confirmed before the names arrive (they are then not stored).

#include "providers/web_metadata.hpp"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pmm::services {
struct ImportPreview;
}

namespace pmm::app {

class Backend;
struct WorkshopLookup;

class ShareController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastExportCode READ lastExportCode NOTIFY lastExportCodeChanged)
    Q_PROPERTY(QVariantMap workshopIdList READ workshopIdList NOTIFY workshopIdListChanged)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged)
    Q_PROPERTY(bool hasPreview READ hasPreview NOTIFY previewChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString portabilityNote READ portabilityNote CONSTANT)

public:
    explicit ShareController(Backend* backend, QObject* parent = nullptr);
    ~ShareController() override;

    QString lastExportCode() const { return m_lastExportCode; }
    // text, count, leftOutLocal, leftOutDisabled, note, leftOutText; empty
    // until an export finished.
    QVariantMap workshopIdList() const { return m_workshopIdList; }
    QVariantMap preview() const { return m_previewMap; }
    bool hasPreview() const { return !m_previewMap.isEmpty(); }
    bool busy() const { return m_busy > 0; }
    QString portabilityNote() const;

    // The lookup of a preview's missing Workshop mods starts `delayMs` after
    // the preview settled and ends "failed" after `timeoutMs`.
    void setLookupTimings(int delayMs, int timeoutMs);

    // Starts exporting the active playset (the code and the Workshop ID
    // list); returns the current lastExportCode (empty until the new code is
    // ready).
    Q_INVOKABLE QString exportCode();
    Q_INVOKABLE bool copyExportCode();
    Q_INVOKABLE bool copyWorkshopIdList();
    Q_INVOKABLE bool exportToFile(const QUrl& fileUrl);
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE bool previewImport(const QString& text);
    Q_INVOKABLE bool importFromFile(const QUrl& fileUrl);
    Q_INVOKABLE bool confirmImport(const QString& mode, int targetPlaysetId, const QString& name);
    Q_INVOKABLE void clearPreview();

    // Largest file read (the protocol accepts 2 MiB of code or list).
    static constexpr qint64 kMaxCodeFileBytes = qint64{4} * 1024 * 1024;
    // Missing Workshop mods looked up per preview; the others are shown by id.
    static constexpr std::size_t kMaxLookupIds = 1000;
    // Answers kept for the session; the cache starts over beyond this.
    static constexpr std::size_t kMaxCachedItems = 20000;

signals:
    void lastExportCodeChanged();
    void workshopIdListChanged();
    void previewChanged();
    void busyChanged();
    // An export finished (code empty on failure).
    void exported(const QString& code);
    // An import was applied (playsetId -1 on failure).
    void imported(int playsetId);

private:
    enum class Lookup { Off, Waiting, Pending, Done, Partial, Failed };

    void setBusy(bool busy);
    void setLastExportCode(const QString& code);
    void setWorkshopIdList(const QVariantMap& list);
    void showError(const QString& title, const QString& message);

    // Lookup of the current preview's missing Workshop mods.
    void resetLookup();
    void startLookup(std::vector<std::string> ids);
    void sendLookup();
    void lookupAnswered(quint64 generation, WorkshopLookup lookup);
    void lookupTimedOut();
    // Rebuilds the preview with every answer so far (on the worker).
    void applyLookup();
    void setLookupKeys(QVariantMap& map) const;

    Backend* m_backend;
    QString m_lastExportCode;
    QVariantMap m_workshopIdList;
    QVariantMap m_previewMap;
    std::shared_ptr<const services::ImportPreview> m_preview;
    quint64 m_previewGeneration = 0;
    quint64 m_exportGeneration = 0;
    bool m_copyWhenExported = false;
    int m_busy = 0;

    // The preview as previewed, before any answer was applied.
    std::shared_ptr<const services::ImportPreview> m_basePreview;
    Lookup m_lookup = Lookup::Off;
    std::vector<std::string> m_lookupMissing; // the preview's missing Workshop ids
    std::vector<std::string> m_lookupIds;     // still to ask about, at most kMaxLookupIds
    std::size_t m_lookupSkipped = 0;          // over kMaxLookupIds: shown by id
    std::vector<providers::WorkshopItemMetadata> m_lookupItems; // the answers for the preview
    std::map<std::string, providers::WorkshopItemMetadata> m_lookupCache; // GUI thread only
    QTimer m_lookupDelay;
    QTimer m_lookupDeadline;
};

} // namespace pmm::app
