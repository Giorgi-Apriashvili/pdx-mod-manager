#pragma once

// Crash-safe replacement of several files as one unit, journaled on disk
// without a database. Used to apply game configuration before a launch and to
// put the previous configuration back later.
//
// Layout: <journalRoot>/<txid>/journal.json plus one backup file per target
// that existed before. Protocol for apply():
//   1. journal "prepared": targets, whether each existed, backup names and the
//      SHA-256 of the new contents. No target has been touched yet.
//   2. existing targets are copied into the transaction folder (durably).
//   3. journal "applying" (now with backup hashes), then each target is
//      replaced with writeFileAtomic().
//   4. journal "committed".
// A failure in step 3 restores every touched target (or deletes it when it
// did not exist before) and marks the journal "rolled_back". A crash leaves
// "prepared" or "applying" behind; listIncomplete() finds those and recover()
// undoes them. Backups are verified against their recorded hash before any
// restore.
//
// Txids are "<UTC yyyymmddThhmmssmmm>Z-<4-digit sequence>", so they sort by
// creation time. Not thread-safe; use one manager per journal root (pmm-app
// holds a per-profile lock, so one process uses a journal root at a time).

#include "core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::platform {

namespace fs = std::filesystem;

struct FileWrite {
    fs::path target; // absolute
    std::string contents;
};

enum class TransactionState {
    Prepared,   // journal written, backups may be incomplete, targets untouched
    Applying,   // backups complete, targets being replaced
    Committed,  // all targets replaced
    RolledBack, // apply() failed and undid its changes
    Recovered,  // interrupted transaction undone by recover()
    Restored,   // committed transaction undone by restore()
};

const char* toString(TransactionState state) noexcept; // "prepared", "applying", ...
std::optional<TransactionState> transactionStateFromString(std::string_view text) noexcept;

struct TransactionEntry {
    fs::path target;
    bool existed = false;
    std::string backupFile;   // file name inside the transaction folder; empty if !existed
    std::string backupSha256; // hash of the original contents; empty if !existed
    std::string newSha256;    // hash of the contents written by apply()
};

struct TransactionRecord {
    std::string id;
    std::string label;
    TransactionState state = TransactionState::Prepared;
    std::int64_t createdUnixMs = 0;
    std::int64_t updatedUnixMs = 0;
    std::vector<TransactionEntry> entries;
    std::string lastError; // why it was rolled back, if it was
    fs::path directory;    // <journalRoot>/<id>
};

// Test hooks for exercising rollback and crash recovery.
struct FileTransactionFaults {
    // Writing target k (0-based) fails with IoError before touching it.
    std::optional<std::size_t> failWriteAt;
    // After k successful writes apply() stops as if the process died: the
    // journal stays "applying" and Interrupted is returned without rollback.
    std::optional<std::size_t> crashAfterWrites;
};

class FileTransactionManager {
public:
    explicit FileTransactionManager(fs::path journalRoot);

    const fs::path& journalRoot() const noexcept { return m_root; }

    // Replaces all targets or none. Targets must be absolute, distinct, and not
    // folders. Returns the committed record, or the error that caused the
    // rollback. If even the rollback fails the journal stays "applying" so
    // that recover() can finish the job later.
    Result<TransactionRecord> apply(std::string label, std::vector<FileWrite> writes);

    // All transactions, oldest first. Folders without a readable journal are
    // skipped.
    Result<std::vector<TransactionRecord>> list() const;
    // Transactions left "prepared" or "applying" by a crash.
    Result<std::vector<TransactionRecord>> listIncomplete() const;
    Result<std::vector<TransactionRecord>> listCommitted() const;
    Result<TransactionRecord> load(std::string_view txid) const;

    // Undoes an interrupted transaction: restores the backed-up originals and
    // deletes targets that did not exist before (only if they still hold the
    // contents apply() wrote). Marks it "recovered".
    Result<TransactionRecord> recover(std::string_view txid);

    // Puts back the originals saved by a committed transaction ("restore the
    // configuration from before launch"); files it created are deleted.
    // Marks it "restored"; may be repeated.
    Result<TransactionRecord> restore(std::string_view txid);

    // Deletes the oldest finished transactions so that at most `keepLast`
    // remain. Interrupted ones are never deleted.
    Status prune(std::size_t keepLast);

    void setFaultsForTesting(FileTransactionFaults faults) { m_faults = faults; }

private:
    fs::path m_root;
    FileTransactionFaults m_faults;
};

} // namespace pmm::platform
