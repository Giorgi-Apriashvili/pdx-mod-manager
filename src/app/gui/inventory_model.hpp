#pragma once

// Installed mods of the current game, for the "Add mods" dialog. inPlayset
// marks mods already in the active playset; `filter` narrows the rows by name
// or provider id.

#include "app/core/view_data.hpp"

#include <QAbstractListModel>
#include <QSet>

#include <vector>

namespace pmm::app {

class Backend;

class InventoryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)

public:
    enum Role {
        IdentityKeyRole = Qt::UserRole + 1,
        NameRole,
        SourceRole,
        SourceLabelRole,
        ProviderIdRole,
        VersionRole,
        InPlaysetRole,
        StateTextRole,
        ProblemsRole,
    };

    explicit InventoryModel(Backend* backend, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_visible.size()); }
    QString filter() const { return m_filter; }
    void setFilter(const QString& filter);

    // Reloads the installed mods of the current game.
    Q_INVOKABLE void refresh();

    // Backend side.
    void setRows(std::vector<InventoryRowData> rows);
    void setPlaysetKeys(QSet<QString> keys);
    int totalCount() const { return static_cast<int>(m_rows.size()); }

signals:
    void countChanged();
    void filterChanged();

private:
    void rebuild();

    Backend* m_backend;
    std::vector<InventoryRowData> m_rows;
    std::vector<std::size_t> m_visible;
    QSet<QString> m_inPlayset;
    QString m_filter;
};

} // namespace pmm::app
