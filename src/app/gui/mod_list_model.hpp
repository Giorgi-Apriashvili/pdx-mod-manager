#pragma once

// Entries of the active playset in load order (source model of modView).
//
// Edits are applied optimistically on the GUI thread with the same core
// functions the services use (core/playset_ops), sent to the services worker
// (PlaysetService: persisted immediately, undoable), and reconciled with the
// authoritative state the worker returns once no other edit is in flight.
// Updates are incremental: changed rows get dataChanged, removed and added
// rows rowsRemoved/rowsInserted. Reorders are reported as dataChanged over the
// affected range: QSortFilterProxyModel turns source row moves into a layout
// change without hint, which makes QML views rebuild and scroll to the top.
// Only a switch to another playset resets the model.
//
// Row arguments are source rows (0-based load-order positions).

#include "app/core/view_data.hpp"

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace pmm::app {

class Backend;
class ModFilterModel;

class ModListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int enabledCount READ enabledCount NOTIFY countsChanged)
    Q_PROPERTY(int readyCount READ readyCount NOTIFY countsChanged)
    Q_PROPERTY(int missingCount READ missingCount NOTIFY countsChanged)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY countsChanged)
    Q_PROPERTY(int blockingCount READ blockingCount NOTIFY countsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY undoChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    enum Role {
        PositionRole = Qt::UserRole + 1,
        EnabledRole,
        NameRole,
        SourceRole,
        SourceLabelRole,
        ProviderIdRole,
        StatusRole,
        StatusTextRole,
        WarningsRole,
        HasWarningsRole,
        ProgressRole,
        BlockingRole,
        IdentityKeyRole,
        VersionRole,
    };

    explicit ModListModel(Backend* backend, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_rows.size()); }
    int enabledCount() const { return m_counts.enabled; }
    int readyCount() const { return m_counts.ready; }
    int missingCount() const { return m_counts.missing; }
    int warningCount() const { return m_counts.warning; }
    int blockingCount() const { return m_counts.blocking; }
    bool canUndo() const { return m_undo.canUndo; }
    bool canRedo() const { return m_undo.canRedo; }
    QString undoText() const { return m_undo.undoText; }
    QString redoText() const { return m_undo.redoText; }
    bool busy() const { return m_pendingEdits > 0; }

    Q_INVOKABLE void setEnabled(int row, bool on);
    // Dependency feedback arrives through dependencyFeedback(); returns "".
    Q_INVOKABLE QString setEnabledRows(const QList<int>& rows, bool on);
    Q_INVOKABLE void moveRow(int from, int to);
    Q_INVOKABLE void moveUp(int row);
    Q_INVOKABLE void moveDown(int row);

    // The contract's moveRows/removeRows overload QAbstractItemModel's
    // virtual functions of the same name; those keep working.
    using QAbstractListModel::moveRows;
    using QAbstractListModel::removeRows;
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4263) // overload, not an override
#endif
    // One undo step for a multi-row move: delta -1/+1 moves every selected
    // entry one step (keyboard semantics); larger deltas gather the selection
    // into a block whose first entry lands at rows[0] + delta (drag and drop).
    Q_INVOKABLE void moveRows(const QList<int>& rows, int delta);
    Q_INVOKABLE void removeRows(const QList<int>& rows);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    Q_INVOKABLE void addMods(const QStringList& identityKeys);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Role values of one row by role name (tests, scripting).
    Q_INVOKABLE QVariantMap get(int row) const;

    // Backend side.
    void setFilterModel(ModFilterModel* filter) { m_filter = filter; }
    std::int64_t playsetId() const noexcept { return m_playsetId; }
    QString game() const { return m_game; }
    // Another playset (or none): resets the model.
    void resetTo(PlaysetStateData state);
    // Same playset: incremental. Ignored while edits are in flight (their
    // result carries a newer state).
    void applyState(PlaysetStateData state);
    const ModRowData* row(int index) const;
    const std::vector<ModRowData>& rows() const noexcept { return m_rows; }
    QSet<QString> keys() const;
    bool hasPendingEdits() const noexcept { return m_pendingEdits > 0; }

signals:
    void countChanged();
    void countsChanged();
    void undoChanged();
    void busyChanged();
    // Dependency issues introduced by enabling mods (empty: none).
    void dependencyFeedback(const QString& text);
    // An authoritative state was applied after edits.
    void edited();

private:
    struct EditOutcome {
        std::optional<Error> error;
        QString feedback;
    };
    using EditOp = std::function<EditOutcome(AppRuntime&, std::int64_t playsetId)>;

    void runEdit(const QString& failureTitle, EditOp op, bool affectsLaunch, bool undoable = true);
    void finishEdit(std::int64_t playsetId, const QString& failureTitle, EditOutcome outcome,
                    PlaysetStateData state, std::optional<LaunchData> launch);
    bool refuseReorder();
    ViewFilter viewFilter() const;
    std::vector<std::size_t> validRows(const QList<int>& rows) const;
    std::vector<PlaysetEntry> entries() const;
    // Puts the rows into the order of `ordered` (same identities).
    void applyOrder(const std::vector<PlaysetEntry>& ordered);
    void reconcile(std::vector<ModRowData> target);
    void emitChangedRuns(const std::vector<char>& changed);
    void recount();
    void setUndo(const UndoData& undo);
    void setCounts(const CountsData& counts);

    Backend* m_backend;
    ModFilterModel* m_filter = nullptr;
    std::int64_t m_playsetId = -1;
    QString m_game;
    QString m_name;
    std::vector<ModRowData> m_rows;
    CountsData m_counts;
    UndoData m_undo;
    int m_pendingEdits = 0;
    std::optional<LaunchData> m_latestLaunch; // from results skipped while edits were pending
};

} // namespace pmm::app
