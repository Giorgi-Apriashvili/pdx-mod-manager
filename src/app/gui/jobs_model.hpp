#pragma once

// Workshop acquisition jobs of the current game (AcquisitionSnapshot), updated
// in place by job id so progress updates do not rebuild the list.

#include "app/core/view_data.hpp"

#include <QAbstractListModel>

#include <vector>

namespace pmm::app {

class JobsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        JobIdRole = Qt::UserRole + 1,
        NameRole,
        ProviderIdRole,
        StateRole,
        StateTextRole,
        ProgressRole,
        ErrorMessageRole,
        CanRetryRole,
        CanOpenFallbackRole,
    };

    explicit JobsModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_rows.size()); }
    void setRows(std::vector<JobRowData> rows);
    const std::vector<JobRowData>& rows() const noexcept { return m_rows; }

signals:
    void countChanged();

private:
    std::vector<JobRowData> m_rows;
};

} // namespace pmm::app
