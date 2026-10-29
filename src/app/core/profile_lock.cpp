#include "app/core/profile_lock.hpp"

#include "app/core/qt_util.hpp"
#include "platform/fs.hpp"

#include <QLockFile>

#include <string>
#include <system_error>
#include <utility>

namespace pmm::app {

Result<std::unique_ptr<ProfileLock>> ProfileLock::acquire(const std::filesystem::path& dataDir,
                                                          std::chrono::milliseconds wait) {
    const std::string folder = platform::pathToUtf8(dataDir);
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);
    if (ec)
        return Error(ErrorCode::IoError,
                     "The application folder " + folder +
                         " could not be created. Check that it is writable.",
                     ec.message());

    auto lock = std::make_unique<QLockFile>(qs(platform::pathToUtf8(dataDir / kFileName)));
    lock->setStaleLockTime(0); // stale only when the owning process is gone, never by age
    if (lock->tryLock(static_cast<int>(wait.count())))
        return std::unique_ptr<ProfileLock>(new ProfileLock(std::move(lock)));

    switch (lock->error()) {
    case QLockFile::LockFailedError: {
        qint64 pid = 0;
        QString host;
        QString application;
        lock->getLockInfo(&pid, &host, &application);
        return Error(ErrorCode::AlreadyExists,
                     "Paradox Mod Manager is already running for this profile (" + folder +
                         "). Switch to the open window, or close it and start again.",
                     "lock held by process " + std::to_string(pid));
    }
    case QLockFile::PermissionError:
        return Error(ErrorCode::PermissionDenied,
                     "The application folder " + folder +
                         " is not writable, so Paradox Mod Manager cannot use it. Check the "
                         "folder's permissions.");
    case QLockFile::NoError:
    case QLockFile::UnknownError:
        break;
    }
    return Error(ErrorCode::IoError,
                 "The application folder " + folder +
                     " could not be locked for this instance. Check that it is writable.");
}

ProfileLock::ProfileLock(std::unique_ptr<QLockFile> lock) : m_lock(std::move(lock)) {}

ProfileLock::~ProfileLock() = default; // QLockFile unlocks on destruction

} // namespace pmm::app
