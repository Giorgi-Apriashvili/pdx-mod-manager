#include "app/gui/mod_details.hpp"

#include "app/core/qt_util.hpp"
#include "app/gui/mod_list_model.hpp"
#include "app/gui/notification_center.hpp"
#include "core/catalog.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

namespace pmm::app {

ModDetails::ModDetails(ModListModel* mods, NotificationCenter* notifications, QObject* parent)
    : QObject(parent), m_mods(mods), m_notifications(notifications) {
    const auto refresh = [this] { emit changed(); };
    connect(mods, &QAbstractItemModel::dataChanged, this, refresh);
    connect(mods, &QAbstractItemModel::modelReset, this, refresh);
    connect(mods, &QAbstractItemModel::rowsInserted, this, refresh);
    connect(mods, &QAbstractItemModel::rowsRemoved, this, refresh);
    connect(mods, &QAbstractItemModel::rowsMoved, this, refresh);
    connect(mods, &QAbstractItemModel::layoutChanged, this, refresh);
}

void ModDetails::setRow(int row) {
    if (row < -1)
        row = -1;
    if (row == m_row)
        return;
    m_row = row;
    emit rowChanged();
    emit changed();
}

const ModRowData* ModDetails::current() const {
    return m_mods->row(m_row);
}

bool ModDetails::hasSelection() const {
    return current() != nullptr;
}

#define PMM_DETAIL(type, getter, expression)                                                       \
    type ModDetails::getter() const {                                                              \
        const ModRowData* r = current();                                                           \
        return r ? type(expression) : type();                                                      \
    }

PMM_DETAIL(QString, name, r->name)
PMM_DETAIL(QString, source, r->source)
PMM_DETAIL(QString, sourceLabel, r->sourceLabel)
PMM_DETAIL(QString, providerId, r->providerId)
PMM_DETAIL(QString, version, r->version)
PMM_DETAIL(QString, supportedVersion, r->supportedVersion)
PMM_DETAIL(QString, statusText, r->statusText)
PMM_DETAIL(QStringList, tags, r->tags)
PMM_DETAIL(QStringList, warnings, r->warnings)
PMM_DETAIL(QStringList, problems, r->problems)
PMM_DETAIL(QString, workshopUrl, r->workshopUrl)
PMM_DETAIL(QString, contentPath, r->contentPath)
#undef PMM_DETAIL

bool ModDetails::canOpenFolder() const {
    const ModRowData* r = current();
    return r && r->canOpenFolder;
}

bool ModDetails::updateAvailable() const {
    const ModRowData* r = current();
    return r && r->updateAvailable;
}

bool ModDetails::canReinstall() const {
    const ModRowData* r = current();
    // Installed (it has a content folder), and no job is changing it.
    return r && r->source == QLatin1String(source::SteamWorkshop) && r->canOpenFolder &&
           r->status != QLatin1String("pending") && r->status != QLatin1String("downloading");
}

QVariantList ModDetails::dependencies() const {
    QVariantList out;
    const ModRowData* r = current();
    if (!r)
        return out;
    for (const DependencyData& dependency : r->dependencies) {
        out.push_back(QVariantMap{{QStringLiteral("name"), dependency.name},
                                  {QStringLiteral("state"), dependency.state},
                                  {QStringLiteral("text"), dependency.text}});
    }
    return out;
}

bool ModDetails::dependenciesLoadAfter() const {
    const ModRowData* r = current();
    return r && dependencyMeaningOf(r->entry.identity.game) != DependencyMeaning::Required;
}

QString ModDetails::dependenciesNote() const {
    const ModRowData* r = current();
    const GameCatalogEntry* game = r ? findGame(r->entry.identity.game) : nullptr;
    if (!game || game->dependencyMeaning == DependencyMeaning::Required)
        return {};
    // Only what is established: Hearts of Iron IV's wiki documents the order;
    // in the other games neither the game nor its launcher reports a named mod
    // missing or out of order, and what the game does with them is untested.
    if (game->dependencyLoadAfterDocumented)
        return tr("%1 loads this mod after these mods when they are enabled. It does not need "
                  "them.")
            .arg(qs(game->displayName));
    return tr("%1 does not need these mods. This mod names them to load before it when they are "
              "enabled.")
        .arg(qs(game->displayName));
}

bool ModDetails::isTrustedWorkshopUrl(const QString& url) {
    const QUrl parsed(url, QUrl::StrictMode);
    return parsed.isValid() && parsed.scheme() == QLatin1String("https") &&
           parsed.host() == QLatin1String("steamcommunity.com") && parsed.userInfo().isEmpty() &&
           parsed.port() == -1;
}

void ModDetails::openWorkshopPage() {
    const QString url = workshopUrl();
    if (url.isEmpty() || !isTrustedWorkshopUrl(url))
        return;
    if (!QDesktopServices::openUrl(QUrl(url)))
        m_notifications->error(tr("Could not open the Workshop page"),
                               tr("No web browser accepted %1. Open it manually.").arg(url));
}

void ModDetails::openContentFolder() {
    const QString path = contentPath();
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    // Archives (legacy zipped mods): show the folder that contains them.
    const QString folder = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (!QFileInfo(folder).isDir()) {
        m_notifications->warning(tr("Folder not found"),
                                 tr("%1 no longer exists. Rescan to update the list.")
                                     .arg(QDir::toNativeSeparators(folder)));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))
        m_notifications->error(tr("Could not open the folder"), QDir::toNativeSeparators(folder));
}

} // namespace pmm::app
