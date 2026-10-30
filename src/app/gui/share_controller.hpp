#pragma once

// Share-code export and import (docs/ui-contract.md). Exports and previews
// run on the services worker: exportCode() starts an export and the code
// arrives in lastExportCode; previewImport() validates without side effects and
// fills `preview`; confirmImport() applies the previewed code (new playset by
// default, or an explicit replace) and queues acquisition of missing Workshop
// entries. A code for another supported game is imported as a new playset of
// that game, and the window switches to it.

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <cstdint>
#include <memory>

namespace pmm::services {
struct ImportPreview;
}

namespace pmm::app {

class Backend;

class ShareController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastExportCode READ lastExportCode NOTIFY lastExportCodeChanged)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged)
    Q_PROPERTY(bool hasPreview READ hasPreview NOTIFY previewChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString portabilityNote READ portabilityNote CONSTANT)

public:
    explicit ShareController(Backend* backend, QObject* parent = nullptr);
    ~ShareController() override;

    QString lastExportCode() const { return m_lastExportCode; }
    QVariantMap preview() const { return m_previewMap; }
    bool hasPreview() const { return !m_previewMap.isEmpty(); }
    bool busy() const { return m_busy > 0; }
    QString portabilityNote() const;

    // Starts exporting the active playset; returns the current lastExportCode
    // (empty until the new code is ready).
    Q_INVOKABLE QString exportCode();
    Q_INVOKABLE bool copyExportCode();
    Q_INVOKABLE bool exportToFile(const QUrl& fileUrl);
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE bool previewImport(const QString& text);
    Q_INVOKABLE bool importFromFile(const QUrl& fileUrl);
    Q_INVOKABLE bool confirmImport(const QString& mode, int targetPlaysetId, const QString& name);
    Q_INVOKABLE void clearPreview();

    // Largest share-code file read (the protocol accepts 2 MiB of code).
    static constexpr qint64 kMaxCodeFileBytes = qint64{4} * 1024 * 1024;

signals:
    void lastExportCodeChanged();
    void previewChanged();
    void busyChanged();
    // An export finished (code empty on failure).
    void exported(const QString& code);
    // An import was applied (playsetId -1 on failure).
    void imported(int playsetId);

private:
    void setBusy(bool busy);
    void setLastExportCode(const QString& code);
    void showError(const QString& title, const QString& message);

    Backend* m_backend;
    QString m_lastExportCode;
    QVariantMap m_previewMap;
    std::shared_ptr<const services::ImportPreview> m_preview;
    quint64 m_previewGeneration = 0;
    quint64 m_exportGeneration = 0;
    bool m_copyWhenExported = false;
    int m_busy = 0;
};

} // namespace pmm::app
