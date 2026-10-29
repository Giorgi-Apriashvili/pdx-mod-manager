#pragma once

// Left sidebar: every catalog game with its installation status. Selection,
// rescans and manual locations are forwarded to the Backend, which runs them
// on the services worker; results arrive through setRows().

#include "app/core/view_data.hpp"

#include <QAbstractListModel>
#include <QUrl>

#include <vector>

namespace pmm::app {

class Backend;

class GamesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString currentKey READ currentKey NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(bool currentInstalled READ currentInstalled NOTIFY currentChanged)
    Q_PROPERTY(bool currentSupported READ currentSupported NOTIFY currentChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        NameRole,
        SupportedRole,
        InstalledRole,
        StatusTextRole,
        VersionRole,
        InstallPathRole,
        UserDataPathRole,
        ManualLocationRole,
    };

    explicit GamesModel(Backend* backend, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString currentKey() const { return m_current; }
    QString currentName() const;
    bool currentInstalled() const;
    bool currentSupported() const;
    bool scanning() const { return m_scanning; }
    int count() const { return static_cast<int>(m_rows.size()); }

    Q_INVOKABLE void select(const QString& key);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE bool locate(const QString& key, const QUrl& installDirUrl,
                            const QUrl& userDataDirUrl);
    Q_INVOKABLE void clearManualLocation(const QString& key);
    Q_INVOKABLE bool importCurrentConfiguration();

    // Backend side.
    void setRows(std::vector<GameRowData> rows);
    void updateRow(const GameRowData& row);
    void setCurrent(const QString& key);
    void setScanning(bool scanning);
    const GameRowData* find(const QString& key) const;
    const std::vector<GameRowData>& rows() const { return m_rows; }

signals:
    void currentChanged();
    void scanningChanged();
    void countChanged();

private:
    Backend* m_backend;
    std::vector<GameRowData> m_rows;
    QString m_current;
    bool m_scanning = false;
};

} // namespace pmm::app
