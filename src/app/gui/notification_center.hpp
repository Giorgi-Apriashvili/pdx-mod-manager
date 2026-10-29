#pragma once

// Transient, user-visible messages ("info" | "warning" | "error"). Every
// failure the user should know about ends up here with actionable text;
// warnings and errors are also written to the log.

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace pmm::app {

class NotificationCenter : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap latest READ latest NOTIFY latestChanged)

public:
    explicit NotificationCenter(QObject* parent = nullptr);

    QVariantMap latest() const { return m_latest; }

    Q_INVOKABLE void post(const QString& level, const QString& title, const QString& text);
    Q_INVOKABLE void dismiss();

    void info(const QString& title, const QString& text) {
        post(QStringLiteral("info"), title, text);
    }
    void warning(const QString& title, const QString& text) {
        post(QStringLiteral("warning"), title, text);
    }
    void error(const QString& title, const QString& text) {
        post(QStringLiteral("error"), title, text);
    }

signals:
    void posted(const QString& level, const QString& title, const QString& text);
    void latestChanged();

private:
    QVariantMap m_latest;
};

} // namespace pmm::app
