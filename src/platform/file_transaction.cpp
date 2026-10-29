#include "platform/file_transaction.hpp"

#include "core/log.hpp"
#include "core/sha256.hpp"
#include "platform/detail/os_util.hpp"
#include "platform/fs.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <system_error>
#include <utility>

namespace pmm::platform {
namespace {

using detail::quotedName;
using nlohmann::json;

constexpr const char* kJournalFile = "journal.json";
constexpr const char* kComponent = "platform.transactions";
constexpr int kJournalFormat = 1;
constexpr std::uint64_t kMaxFileBytes = 256ull * 1024 * 1024;
constexpr std::int64_t kOrphanAgeSeconds = 3600;

enum class UndoMode {
    Rollback, // apply() failed in this process
    Recover,  // interrupted by a crash; leave files we did not write alone
    Restore,  // user asked for the pre-change state of a committed transaction
};

std::string makeTransactionId() {
    static std::atomic<std::int64_t> lastMs{0};
    static std::atomic<std::uint32_t> sequence{0};
    // Never step backwards, even if the wall clock does.
    std::int64_t now = nowUnixMs();
    std::int64_t previous = lastMs.load();
    while (!lastMs.compare_exchange_weak(previous, std::max(now, previous))) {
    }
    now = std::max(now, previous);
    const unsigned seq = sequence.fetch_add(1) % 10000;

    const auto seconds = static_cast<std::time_t>(now / 1000);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char id[48];
    std::snprintf(id, sizeof id, "%04d%02d%02dT%02d%02d%02d%03dZ-%04u", utc.tm_year + 1900,
                  utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec,
                  static_cast<int>(now % 1000), seq);
    return id;
}

// Exactly the shape makeTransactionId() produces; also keeps ids from
// smuggling path separators into journal lookups.
bool isValidTransactionId(std::string_view id) noexcept {
    if (id.size() != 24 || id[8] != 'T' || id[18] != 'Z' || id[19] != '-')
        return false;
    for (std::size_t i = 0; i < id.size(); ++i) {
        if (i == 8 || i == 18 || i == 19)
            continue;
        if (id[i] < '0' || id[i] > '9')
            return false;
    }
    return true;
}

bool isSafeBackupName(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 32 && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '.';
    }) && name.find("..") == std::string_view::npos;
}

std::string comparisonKey(const fs::path& path) {
    std::string key = toGenericUtf8(path.lexically_normal());
#if defined(_WIN32)
    std::transform(key.begin(), key.end(), key.begin(), [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    });
#endif
    return key;
}

json toJson(const TransactionRecord& record) {
    json entries = json::array();
    for (const TransactionEntry& entry : record.entries) {
        json item = json::object();
        item["target"] = pathToUtf8(entry.target);
        item["existed"] = entry.existed;
        item["backup"] = entry.backupFile;
        item["backupSha256"] = entry.backupSha256;
        item["newSha256"] = entry.newSha256;
        entries.push_back(std::move(item));
    }
    json root = json::object();
    root["format"] = kJournalFormat;
    root["id"] = record.id;
    root["label"] = record.label;
    root["state"] = toString(record.state);
    root["createdUnixMs"] = record.createdUnixMs;
    root["updatedUnixMs"] = record.updatedUnixMs;
    root["entries"] = std::move(entries);
    if (!record.lastError.empty())
        root["lastError"] = record.lastError;
    return root;
}

Result<TransactionRecord> fromJson(const std::string& text) {
    try {
        const json root = json::parse(text);
        if (!root.is_object())
            return makeError(ErrorCode::ParseError, "The configuration change journal is damaged.",
                             "journal is not an object");
        if (root.value("format", 0) != kJournalFormat)
            return makeError(ErrorCode::UnsupportedVersion,
                             "The configuration change journal was written by a newer version.",
                             "format " + root.value("format", json()).dump());
        TransactionRecord record;
        record.id = root.at("id").get<std::string>();
        record.label = root.value("label", std::string());
        const std::optional<TransactionState> state =
            transactionStateFromString(root.at("state").get<std::string>());
        if (!state)
            return makeError(ErrorCode::ParseError, "The configuration change journal is damaged.",
                             "unknown state");
        record.state = *state;
        record.createdUnixMs = root.value("createdUnixMs", std::int64_t{0});
        record.updatedUnixMs = root.value("updatedUnixMs", std::int64_t{0});
        record.lastError = root.value("lastError", std::string());
        for (const json& item : root.at("entries")) {
            TransactionEntry entry;
            entry.target = pathFromUtf8(item.at("target").get<std::string>());
            entry.existed = item.at("existed").get<bool>();
            entry.backupFile = item.value("backup", std::string());
            entry.backupSha256 = item.value("backupSha256", std::string());
            entry.newSha256 = item.value("newSha256", std::string());
            if (entry.target.empty() || !entry.target.is_absolute() ||
                (entry.existed && !isSafeBackupName(entry.backupFile)))
                return makeError(ErrorCode::ParseError,
                                 "The configuration change journal is damaged.", "invalid entry");
            record.entries.push_back(std::move(entry));
        }
        return record;
    } catch (const json::exception& e) {
        return makeError(ErrorCode::ParseError, "The configuration change journal is damaged.",
                         e.what());
    }
}

Status writeJournal(TransactionRecord& record) {
    record.updatedUnixMs = nowUnixMs();
    // replace: a label with invalid UTF-8 must not make the journal unwritable.
    const std::string text = toJson(record).dump(2, ' ', false, json::error_handler_t::replace);
    return writeFileAtomic(record.directory / kJournalFile, text);
}

// Puts entries [0, count) back to their state before apply(). Every backup is
// read and verified first, so a damaged backup aborts before anything changes.
// Then each entry is processed even if an earlier one fails; the first error
// is returned.
Status undoEntries(const TransactionRecord& record, std::size_t count, UndoMode mode) {
    count = std::min(count, record.entries.size());
    std::vector<std::string> originals(count);
    for (std::size_t i = 0; i < count; ++i) {
        const TransactionEntry& entry = record.entries[i];
        if (!entry.existed)
            continue;
        const fs::path backup = record.directory / entry.backupFile;
        Result<std::string> contents = readFile(backup, kMaxFileBytes);
        if (!contents)
            return makeError(ErrorCode::IntegrityCheckFailed,
                             "The backup of " + quotedName(entry.target) +
                                 " is missing or unreadable, so nothing was restored.",
                             contents.error().detail);
        if (sha256Hex(*contents) != entry.backupSha256)
            return makeError(ErrorCode::IntegrityCheckFailed,
                             "The backup of " + quotedName(entry.target) +
                                 " is damaged, so nothing was restored.",
                             pathToUtf8(backup));
        originals[i] = std::move(contents).value();
    }

    std::optional<Error> firstError;
    for (std::size_t i = 0; i < count; ++i) {
        const TransactionEntry& entry = record.entries[i];
        Status status;
        if (entry.existed) {
            Result<std::string> current = readFile(entry.target, kMaxFileBytes);
            if (current && sha256Hex(*current) == entry.backupSha256)
                continue; // already the original
            status = writeFileAtomic(entry.target, originals[i]);
        } else {
            std::error_code ec;
            if (!fs::exists(entry.target, ec))
                continue;
            if (mode == UndoMode::Recover) {
                Result<std::string> current = readFile(entry.target, kMaxFileBytes);
                if (!current || sha256Hex(*current) != entry.newSha256) {
                    logWarning(kComponent,
                               "left a file that was changed after an interrupted write",
                               {{"id", record.id}, {"target", pathToUtf8(entry.target)}});
                    continue;
                }
            }
            status = removeFile(entry.target);
        }
        if (!status.ok() && !firstError)
            firstError = status.error();
    }
    if (firstError)
        return *firstError;
    return {};
}

bool isIncomplete(TransactionState state) noexcept {
    return state == TransactionState::Prepared || state == TransactionState::Applying;
}

} // namespace

const char* toString(TransactionState state) noexcept {
    switch (state) {
    case TransactionState::Prepared:
        return "prepared";
    case TransactionState::Applying:
        return "applying";
    case TransactionState::Committed:
        return "committed";
    case TransactionState::RolledBack:
        return "rolled_back";
    case TransactionState::Recovered:
        return "recovered";
    case TransactionState::Restored:
        return "restored";
    }
    return "prepared";
}

std::optional<TransactionState> transactionStateFromString(std::string_view text) noexcept {
    for (const TransactionState state :
         {TransactionState::Prepared, TransactionState::Applying, TransactionState::Committed,
          TransactionState::RolledBack, TransactionState::Recovered, TransactionState::Restored}) {
        if (text == toString(state))
            return state;
    }
    return std::nullopt;
}

FileTransactionManager::FileTransactionManager(fs::path journalRoot)
    : m_root(std::move(journalRoot)) {}

Result<TransactionRecord> FileTransactionManager::apply(std::string label,
                                                        std::vector<FileWrite> writes) {
    if (writes.empty())
        return makeError(ErrorCode::InvalidArgument, "There are no files to write.");
    std::vector<std::string> keys;
    for (const FileWrite& write : writes) {
        const std::string utf8 = pathToUtf8(write.target);
        if (!write.target.is_absolute() || !write.target.has_filename())
            return makeError(ErrorCode::InvalidArgument,
                             "Configuration files must be given as absolute file paths.", utf8);
        if (pathFromUtf8(utf8) != write.target)
            return makeError(ErrorCode::InvalidArgument,
                             "The file name " + quotedName(write.target) +
                                 " cannot be recorded for recovery.",
                             utf8);
        std::string key = comparisonKey(write.target);
        if (std::find(keys.begin(), keys.end(), key) != keys.end())
            return makeError(ErrorCode::InvalidArgument,
                             "The file " + quotedName(write.target) + " is listed twice.", utf8);
        keys.push_back(std::move(key));
        std::error_code ec;
        if (fs::is_directory(write.target, ec))
            return makeError(ErrorCode::IoError,
                             "Cannot write " + quotedName(write.target) +
                                 " because a folder with that name exists.",
                             utf8);
    }

    TransactionRecord record;
    record.label = std::move(label);
    record.createdUnixMs = nowUnixMs();
    PMM_TRY(createDirectories(m_root));
    for (int attempt = 0;; ++attempt) {
        record.id = makeTransactionId();
        record.directory = m_root / record.id;
        std::error_code ec;
        if (fs::create_directory(record.directory, ec))
            break;
        if (ec)
            return detail::fsError(ec, ErrorCode::IoError,
                                   "Could not create a backup folder for the configuration change.",
                                   record.directory);
        if (attempt >= 100)
            return makeError(ErrorCode::IoError,
                             "Could not create a backup folder for the configuration change.",
                             pathToUtf8(record.directory));
    }
    detail::syncDirectoryBestEffort(m_root);

    for (std::size_t i = 0; i < writes.size(); ++i) {
        TransactionEntry entry;
        entry.target = writes[i].target;
        std::error_code ec;
        entry.existed = fs::exists(entry.target, ec);
        if (ec) {
            std::error_code ignored;
            fs::remove_all(record.directory, ignored);
            return detail::fsError(ec, ErrorCode::IoError,
                                   "Could not inspect " + quotedName(entry.target) + ".",
                                   entry.target);
        }
        if (entry.existed) {
            char name[32];
            std::snprintf(name, sizeof name, "%04u.bak", static_cast<unsigned>(i));
            entry.backupFile = name;
        }
        entry.newSha256 = sha256Hex(writes[i].contents);
        record.entries.push_back(std::move(entry));
    }

    // Records the failure on a transaction whose targets are all untouched.
    const auto abandon = [&](Error error) -> Error {
        record.state = TransactionState::RolledBack;
        record.lastError = error.message;
        if (!writeJournal(record).ok())
            logWarning(kComponent, "could not record an abandoned change", {{"id", record.id}});
        return error;
    };

    record.state = TransactionState::Prepared;
    if (Status status = writeJournal(record); !status.ok()) {
        std::error_code ignored;
        fs::remove_all(record.directory, ignored);
        return std::move(status).error();
    }

    for (TransactionEntry& entry : record.entries) {
        if (!entry.existed)
            continue;
        Result<std::string> original = readFile(entry.target, kMaxFileBytes);
        if (!original)
            return abandon(std::move(original).error());
        if (Status status = writeFileAtomic(record.directory / entry.backupFile, *original);
            !status.ok())
            return abandon(std::move(status).error());
        entry.backupSha256 = sha256Hex(*original);
    }

    record.state = TransactionState::Applying;
    if (Status status = writeJournal(record); !status.ok())
        return abandon(std::move(status).error());

    // Undoes targets [0, touched) after `error`. If that fails too, the journal
    // stays "applying" so that recover() can finish it later.
    const auto rollBack = [&](std::size_t touched, Error error) -> Error {
        if (Status undo = undoEntries(record, touched, UndoMode::Rollback); !undo.ok()) {
            logError(kComponent, "rollback failed",
                     {{"id", record.id}, {"error", undo.error().message}});
            error.detail += (error.detail.empty() ? "" : "; ") + std::string("rollback failed: ") +
                            undo.error().message + " " + undo.error().detail;
            return error;
        }
        record.state = TransactionState::RolledBack;
        record.lastError = error.message;
        if (!writeJournal(record).ok())
            logWarning(kComponent, "could not record a rollback", {{"id", record.id}});
        logInfo(kComponent, "configuration change rolled back",
                {{"id", record.id}, {"error", error.message}});
        return error;
    };

    const auto simulatedCrash = [&] {
        return makeError(ErrorCode::Interrupted, "Simulated crash while applying files.",
                         record.id);
    };
    for (std::size_t i = 0; i < writes.size(); ++i) {
        if (m_faults.crashAfterWrites && *m_faults.crashAfterWrites == i)
            return simulatedCrash();
        Status status;
        if (m_faults.failWriteAt && *m_faults.failWriteAt == i)
            status = makeError(ErrorCode::IoError, "Injected write failure.",
                               pathToUtf8(record.entries[i].target));
        else
            status = writeFileAtomic(record.entries[i].target, writes[i].contents);
        if (!status.ok())
            return rollBack(i, std::move(status).error());
    }
    if (m_faults.crashAfterWrites && *m_faults.crashAfterWrites >= writes.size())
        return simulatedCrash();

    record.state = TransactionState::Committed;
    if (Status status = writeJournal(record); !status.ok())
        return rollBack(writes.size(), std::move(status).error());
    logInfo(kComponent, "configuration change applied",
            {{"id", record.id},
             {"label", record.label},
             {"files", std::to_string(record.entries.size())}});
    return record;
}

Result<TransactionRecord> FileTransactionManager::load(std::string_view txid) const {
    if (!isValidTransactionId(txid))
        return makeError(ErrorCode::InvalidArgument, "Invalid configuration change id.",
                         std::string(txid));
    const fs::path directory = m_root / std::string(txid);
    Result<std::string> text = readFile(directory / kJournalFile, kMaxFileBytes);
    if (!text) {
        if (text.error().code == ErrorCode::NotFound)
            return makeError(ErrorCode::NotFound,
                             "Configuration change " + std::string(txid) + " was not found.",
                             text.error().detail);
        return std::move(text).error();
    }
    Result<TransactionRecord> record = fromJson(*text);
    if (!record) {
        Error error = std::move(record).error();
        error.detail = pathToUtf8(directory / kJournalFile) + ": " + error.detail;
        return error;
    }
    if (record->id != txid)
        return makeError(ErrorCode::ParseError, "The configuration change journal is damaged.",
                         pathToUtf8(directory / kJournalFile) + ": id mismatch");
    record->directory = directory;
    return record;
}

Result<std::vector<TransactionRecord>> FileTransactionManager::list() const {
    std::vector<TransactionRecord> records;
    std::error_code ec;
    if (!fs::is_directory(m_root, ec))
        return records;
    fs::directory_iterator it(m_root, ec);
    for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code typeError;
        if (!it->is_directory(typeError))
            continue;
        const std::string name = pathToUtf8(it->path().filename());
        if (!isValidTransactionId(name))
            continue;
        Result<TransactionRecord> record = load(name);
        if (!record) {
            logWarning(kComponent, "skipping unreadable transaction",
                       {{"id", name}, {"error", record.error().message}});
            continue;
        }
        records.push_back(std::move(record).value());
    }
    if (ec)
        return detail::fsError(ec, ErrorCode::IoError, "Could not list configuration changes.",
                               m_root);
    std::sort(records.begin(), records.end(),
              [](const TransactionRecord& a, const TransactionRecord& b) { return a.id < b.id; });
    return records;
}

Result<std::vector<TransactionRecord>> FileTransactionManager::listIncomplete() const {
    Result<std::vector<TransactionRecord>> all = list();
    if (!all)
        return all;
    std::vector<TransactionRecord> result;
    for (TransactionRecord& record : *all) {
        if (isIncomplete(record.state))
            result.push_back(std::move(record));
    }
    return result;
}

Result<std::vector<TransactionRecord>> FileTransactionManager::listCommitted() const {
    Result<std::vector<TransactionRecord>> all = list();
    if (!all)
        return all;
    std::vector<TransactionRecord> result;
    for (TransactionRecord& record : *all) {
        if (record.state == TransactionState::Committed)
            result.push_back(std::move(record));
    }
    return result;
}

Result<TransactionRecord> FileTransactionManager::recover(std::string_view txid) {
    Result<TransactionRecord> loaded = load(txid);
    if (!loaded)
        return loaded;
    TransactionRecord record = std::move(loaded).value();
    if (!isIncomplete(record.state))
        return makeError(ErrorCode::InvalidArgument,
                         "Configuration change " + record.id +
                             " was not interrupted, so there is nothing to recover.",
                         toString(record.state));
    // "prepared" means no target was touched yet; only the journal needs closing.
    if (record.state == TransactionState::Applying)
        PMM_TRY(undoEntries(record, record.entries.size(), UndoMode::Recover));
    record.state = TransactionState::Recovered;
    PMM_TRY(writeJournal(record));
    logInfo(kComponent, "interrupted configuration change recovered", {{"id", record.id}});
    return record;
}

Result<TransactionRecord> FileTransactionManager::restore(std::string_view txid) {
    Result<TransactionRecord> loaded = load(txid);
    if (!loaded)
        return loaded;
    TransactionRecord record = std::move(loaded).value();
    if (record.state != TransactionState::Committed && record.state != TransactionState::Restored)
        return makeError(ErrorCode::InvalidArgument,
                         "Only applied configuration changes can be restored.",
                         record.id + ": " + toString(record.state));
    PMM_TRY(undoEntries(record, record.entries.size(), UndoMode::Restore));
    record.state = TransactionState::Restored;
    PMM_TRY(writeJournal(record));
    logInfo(kComponent, "configuration restored", {{"id", record.id}, {"label", record.label}});
    return record;
}

Status FileTransactionManager::prune(std::size_t keepLast) {
    Result<std::vector<TransactionRecord>> all = list();
    if (!all)
        return std::move(all).error();
    std::vector<const TransactionRecord*> finished;
    for (const TransactionRecord& record : *all) {
        if (!isIncomplete(record.state))
            finished.push_back(&record);
    }
    std::optional<Error> firstError;
    const std::size_t excess = finished.size() > keepLast ? finished.size() - keepLast : 0;
    for (std::size_t i = 0; i < excess; ++i) {
        std::error_code ec;
        fs::remove_all(finished[i]->directory, ec);
        if (ec && !firstError)
            firstError = detail::fsError(ec, ErrorCode::IoError,
                                         "Could not delete an old configuration backup.",
                                         finished[i]->directory);
    }

    // Folders left by a crash between creating the folder and writing the
    // first journal hold nothing worth keeping once they are old enough not to
    // belong to an apply() in progress.
    std::error_code ec;
    const std::int64_t cutoff = nowUnixMs() / 1000 - kOrphanAgeSeconds;
    std::vector<fs::path> orphans;
    for (fs::directory_iterator it(m_root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code probe;
        if (it->is_directory(probe) && isValidTransactionId(pathToUtf8(it->path().filename())) &&
            !fs::exists(it->path() / kJournalFile, probe) &&
            lastWriteUnixSeconds(it->path()) <= cutoff)
            orphans.push_back(it->path());
    }
    for (const fs::path& orphan : orphans) {
        std::error_code removeError;
        fs::remove_all(orphan, removeError);
    }
    if (firstError)
        return *firstError;
    return {};
}

} // namespace pmm::platform
