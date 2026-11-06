#pragma once

// User interface settings, persisted in the application database's settings
// table ("ui.*" keys) and restored at start-up before the window is created:
// theme, window geometry, pane and column widths. effectiveDark follows the
// OS colour scheme (QStyleHints::colorScheme) while the theme is "system".
// lastGameKey mirrors the services' "app.lastGameKey" (set by selecting a game).
// The daily update check (docs/updates.md) keeps its switch and the date of its
// last answer under "app.updates.*"; both are written as soon as they change.

#include <QDate>
#include <QList>
#include <QObject>
#include <QString>

#include <map>
#include <string>
#include <vector>

namespace pmm::app {

class ServiceRunner;

class AppSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool effectiveDark READ effectiveDark NOTIFY effectiveDarkChanged)
    Q_PROPERTY(int windowX READ windowX WRITE setWindowX NOTIFY geometryChanged)
    Q_PROPERTY(int windowY READ windowY WRITE setWindowY NOTIFY geometryChanged)
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY geometryChanged)
    Q_PROPERTY(int windowHeight READ windowHeight WRITE setWindowHeight NOTIFY geometryChanged)
    Q_PROPERTY(
        bool windowMaximized READ windowMaximized WRITE setWindowMaximized NOTIFY geometryChanged)
    Q_PROPERTY(int leftPaneWidth READ leftPaneWidth WRITE setLeftPaneWidth NOTIFY paneWidthsChanged)
    Q_PROPERTY(
        int rightPaneWidth READ rightPaneWidth WRITE setRightPaneWidth NOTIFY paneWidthsChanged)
    Q_PROPERTY(
        QList<int> columnWidths READ columnWidths WRITE setColumnWidths NOTIFY columnWidthsChanged)
    Q_PROPERTY(QString lastGameKey READ lastGameKey NOTIFY lastGameKeyChanged)
    Q_PROPERTY(bool checkUpdatesAutomatically READ checkUpdatesAutomatically WRITE
                   setCheckUpdatesAutomatically NOTIFY checkUpdatesAutomaticallyChanged)

public:
    explicit AppSettings(ServiceRunner* runner, QObject* parent = nullptr);

    // Keys read at start-up (ui.* plus app.lastGameKey).
    static std::vector<std::string> storageKeys();
    void load(const std::map<std::string, std::string>& values);

    QString theme() const { return m_theme; }
    void setTheme(const QString& theme);
    bool effectiveDark() const;
    int windowX() const { return m_x; }
    void setWindowX(int value);
    int windowY() const { return m_y; }
    void setWindowY(int value);
    int windowWidth() const { return m_width; }
    void setWindowWidth(int value);
    int windowHeight() const { return m_height; }
    void setWindowHeight(int value);
    bool windowMaximized() const { return m_maximized; }
    void setWindowMaximized(bool value);
    int leftPaneWidth() const { return m_left; }
    void setLeftPaneWidth(int value);
    int rightPaneWidth() const { return m_right; }
    void setRightPaneWidth(int value);
    QList<int> columnWidths() const { return m_columns; }
    void setColumnWidths(const QList<int>& widths);
    QString lastGameKey() const { return m_lastGameKey; }
    void setLastGameKey(const QString& key);
    bool checkUpdatesAutomatically() const { return m_checkUpdates; }
    void setCheckUpdatesAutomatically(bool value);
    // The local date on which the daily update check last got an answer from
    // GitHub; null before the first one.
    QDate lastUpdateCheck() const { return m_lastUpdateCheck; }
    void recordUpdateCheck(const QDate& date);

    // Writes every setting (also done when the application quits).
    Q_INVOKABLE void save();
    // The values save() writes, by storage key.
    std::map<std::string, std::string> values() const;

signals:
    void themeChanged();
    void effectiveDarkChanged();
    void geometryChanged();
    void paneWidthsChanged();
    void columnWidthsChanged();
    void lastGameKeyChanged();
    void checkUpdatesAutomaticallyChanged();

private:
    void store(std::map<std::string, std::string> values) const;

    ServiceRunner* m_runner;
    QString m_theme = QStringLiteral("system");
    int m_x = 0;
    int m_y = 0;
    int m_width = 0;
    int m_height = 0;
    bool m_maximized = false;
    int m_left = 0;
    int m_right = 0;
    QList<int> m_columns;
    QString m_lastGameKey;
    bool m_checkUpdates = true;
    QDate m_lastUpdateCheck;
};

} // namespace pmm::app
