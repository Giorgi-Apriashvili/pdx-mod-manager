#include "app/gui/notification_center.hpp"

#include "app/core/qt_util.hpp"
#include "core/log.hpp"

namespace pmm::app {

NotificationCenter::NotificationCenter(QObject* parent) : QObject(parent) {}

void NotificationCenter::post(const QString& level, const QString& title, const QString& text) {
    if (level == QLatin1String("error"))
        logError("app.notify", utf8(title), {{"text", utf8(text)}});
    else if (level == QLatin1String("warning"))
        logWarning("app.notify", utf8(title), {{"text", utf8(text)}});
    m_latest = QVariantMap{{QStringLiteral("level"), level},
                           {QStringLiteral("title"), title},
                           {QStringLiteral("text"), text}};
    emit latestChanged();
    emit posted(level, title, text);
}

void NotificationCenter::dismiss() {
    if (m_latest.isEmpty())
        return;
    m_latest.clear();
    emit latestChanged();
}

} // namespace pmm::app
