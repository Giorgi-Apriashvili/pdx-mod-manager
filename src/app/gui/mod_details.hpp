#pragma once

// Details of the selected mod list row (a source row of ModListModel). The
// Workshop page and the content folder are opened with QDesktopServices; the
// only URLs ever opened are https://steamcommunity.com/ pages built from
// validated provider ids (providers/fallback), never text from share codes.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace pmm::app {

class ModListModel;
class NotificationCenter;
struct ModRowData;

class ModDetails : public QObject {
    Q_OBJECT
    Q_PROPERTY(int row READ row WRITE setRow NOTIFY rowChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(QString sourceLabel READ sourceLabel NOTIFY changed)
    Q_PROPERTY(QString providerId READ providerId NOTIFY changed)
    Q_PROPERTY(QString version READ version NOTIFY changed)
    Q_PROPERTY(QString supportedVersion READ supportedVersion NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QStringList tags READ tags NOTIFY changed)
    Q_PROPERTY(QVariantList dependencies READ dependencies NOTIFY changed)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY changed)
    Q_PROPERTY(QStringList problems READ problems NOTIFY changed)
    Q_PROPERTY(QString workshopUrl READ workshopUrl NOTIFY changed)
    Q_PROPERTY(QString contentPath READ contentPath NOTIFY changed)
    Q_PROPERTY(bool canOpenFolder READ canOpenFolder NOTIFY changed)

public:
    ModDetails(ModListModel* mods, NotificationCenter* notifications, QObject* parent = nullptr);

    int row() const { return m_row; }
    void setRow(int row);

    bool hasSelection() const;
    QString name() const;
    QString source() const;
    QString sourceLabel() const;
    QString providerId() const;
    QString version() const;
    QString supportedVersion() const;
    QString statusText() const;
    QStringList tags() const;
    QVariantList dependencies() const;
    QStringList warnings() const;
    QStringList problems() const;
    QString workshopUrl() const;
    QString contentPath() const;
    bool canOpenFolder() const;

    Q_INVOKABLE void openWorkshopPage();
    Q_INVOKABLE void openContentFolder();

    // True for the https://steamcommunity.com/ pages the app builds itself.
    static bool isTrustedWorkshopUrl(const QString& url);

signals:
    void rowChanged();
    void changed();

private:
    const ModRowData* current() const;

    ModListModel* m_mods;
    NotificationCenter* m_notifications;
    int m_row = -1;
};

} // namespace pmm::app
