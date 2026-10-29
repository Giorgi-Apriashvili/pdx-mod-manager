#pragma once

// Exclusive lock on an application profile: <dataDir>/pmm-app.lock. The
// configuration-transaction journal, the acquisition jobs and the Steam helper
// sessions (one helper per game) assume a single running instance per
// profile, so pmm-app takes this lock before it opens anything and keeps it
// until it has shut down. A lock left behind by a crashed process is
// recognised by its process id and taken over.

#include "core/result.hpp"

#include <chrono>
#include <filesystem>
#include <memory>

class QLockFile;

namespace pmm::app {

class ProfileLock {
public:
    static constexpr const char* kFileName = "pmm-app.lock";

    // Creates `dataDir` if needed. Waits up to `wait` for another instance to
    // release the lock (a quick restart), then fails with AlreadyExists while
    // a running instance holds it. Messages are shown to the user as they are.
    static Result<std::unique_ptr<ProfileLock>>
    acquire(const std::filesystem::path& dataDir,
            std::chrono::milliseconds wait = std::chrono::milliseconds(2000));

    ~ProfileLock();
    ProfileLock(const ProfileLock&) = delete;
    ProfileLock& operator=(const ProfileLock&) = delete;

private:
    explicit ProfileLock(std::unique_ptr<QLockFile> lock);

    std::unique_ptr<QLockFile> m_lock;
};

} // namespace pmm::app
