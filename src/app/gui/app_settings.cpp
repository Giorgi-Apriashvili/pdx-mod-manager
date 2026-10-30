#include "app/gui/app_settings.hpp"

#include "app/core/qt_util.hpp"
#include "app/core/service_runner.hpp"
#include "core/log.hpp"
#include "persistence/settings_repository.hpp"
#include "services/service_context.hpp"

#include <QGuiApplication>
#include <QStyleHints>

namespace pmm::app {
namespace {

constexpr const char* kTheme = "ui.theme";
constexpr const char* kX = "ui.window.x";
constexpr const char* kY = "ui.window.y";
constexpr const char* kWidth = "ui.window.width";
constexpr const char* kHeight = "ui.window.height";
constexpr const char* kMaximized = "ui.window.maximized";
constexpr const char* kLeft = "ui.pane.left";
constexpr const char* kRight = "ui.pane.right";
constexpr const char* kColumns = "ui.columns";

int toInt(const std::map<std::string, std::string>& values, const char* key, int fallback) {
    const auto it = values.find(key);
    if (it == values.end())
        return fallback;
    bool ok = false;
    const int value = qs(it->second).toInt(&ok);
    return ok ? value : fallback;
}

} // namespace

AppSettings::AppSettings(ServiceRunner* runner, QObject* parent)
    : QObject(parent), m_runner(runner) {
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(app->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
            if (m_theme == QLatin1String("system"))
                emit effectiveDarkChanged();
        });
    }
}

std::vector<std::string> AppSettings::storageKeys() {
    return {kTheme,     kX,    kY,     kWidth,   kHeight,
            kMaximized, kLeft, kRight, kColumns, services::settings_key::LastGame};
}

void AppSettings::load(const std::map<std::string, std::string>& values) {
    if (const auto it = values.find(kTheme); it != values.end())
        setTheme(qs(it->second));
    m_x = toInt(values, kX, 0);
    m_y = toInt(values, kY, 0);
    m_width = toInt(values, kWidth, 0);
    m_height = toInt(values, kHeight, 0);
    m_maximized = values.count(kMaximized) && values.at(kMaximized) == "true";
    m_left = toInt(values, kLeft, 0);
    m_right = toInt(values, kRight, 0);
    m_columns.clear();
    if (const auto it = values.find(kColumns); it != values.end()) {
        for (const QString& part : qs(it->second).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            bool ok = false;
            const int width = part.trimmed().toInt(&ok);
            if (ok)
                m_columns.push_back(width);
        }
    }
    if (const auto it = values.find(services::settings_key::LastGame); it != values.end())
        m_lastGameKey = qs(it->second);
    emit geometryChanged();
    emit paneWidthsChanged();
    emit columnWidthsChanged();
    emit lastGameKeyChanged();
}

void AppSettings::setTheme(const QString& theme) {
    const QString normalized = theme == QLatin1String("light") || theme == QLatin1String("dark")
                                   ? theme
                                   : QStringLiteral("system");
    if (normalized == m_theme)
        return;
    const bool wasDark = effectiveDark();
    m_theme = normalized;
    emit themeChanged();
    if (wasDark != effectiveDark())
        emit effectiveDarkChanged();
}

bool AppSettings::effectiveDark() const {
    if (m_theme == QLatin1String("dark"))
        return true;
    if (m_theme == QLatin1String("light"))
        return false;
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        return app->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    return false;
}

#define PMM_SETTER(name, member, signal)                                                           \
    void AppSettings::name(int value) {                                                            \
        if ((member) == value)                                                                     \
            return;                                                                                \
        (member) = value;                                                                          \
        emit signal();                                                                             \
    }

PMM_SETTER(setWindowX, m_x, geometryChanged)
PMM_SETTER(setWindowY, m_y, geometryChanged)
PMM_SETTER(setWindowWidth, m_width, geometryChanged)
PMM_SETTER(setWindowHeight, m_height, geometryChanged)
PMM_SETTER(setLeftPaneWidth, m_left, paneWidthsChanged)
PMM_SETTER(setRightPaneWidth, m_right, paneWidthsChanged)
#undef PMM_SETTER

void AppSettings::setWindowMaximized(bool value) {
    if (m_maximized == value)
        return;
    m_maximized = value;
    emit geometryChanged();
}

void AppSettings::setColumnWidths(const QList<int>& widths) {
    if (m_columns == widths)
        return;
    m_columns = widths;
    emit columnWidthsChanged();
}

void AppSettings::setLastGameKey(const QString& key) {
    if (m_lastGameKey == key)
        return;
    m_lastGameKey = key;
    emit lastGameKeyChanged();
}

std::map<std::string, std::string> AppSettings::values() const {
    QStringList columns;
    for (int width : m_columns)
        columns.push_back(QString::number(width));
    return {{kTheme, utf8(m_theme)},
            {kX, std::to_string(m_x)},
            {kY, std::to_string(m_y)},
            {kWidth, std::to_string(m_width)},
            {kHeight, std::to_string(m_height)},
            {kMaximized, m_maximized ? "true" : "false"},
            {kLeft, std::to_string(m_left)},
            {kRight, std::to_string(m_right)},
            {kColumns, utf8(columns.join(QLatin1Char(',')))}};
}

void AppSettings::save() {
    if (!m_runner || !m_runner->isRunning())
        return;
    m_runner->post([values = values()](AppRuntime& runtime) {
        persistence::SettingsRepository settings(runtime.database());
        const Status saved = runtime.database().transaction([&]() -> Status {
            for (const auto& [key, value] : values)
                PMM_TRY(settings.set(key, value));
            return {};
        });
        if (!saved)
            logWarning("app.settings", "saving settings failed",
                       {{"error", saved.error().message}});
    });
}

} // namespace pmm::app
