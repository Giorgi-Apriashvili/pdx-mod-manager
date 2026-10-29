#include "platform/fs.hpp"

#include "platform/detail/os_util.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace pmm::platform {
namespace {

using detail::osError;
using detail::quotedName;

constexpr std::size_t kChunkSize = std::size_t{1} << 20;
constexpr std::size_t kMaxIoPerCall = std::size_t{1} << 30;

// Last OS error, never 0 (some failing calls leave the error unset).
int failureCode() noexcept {
    const int error = detail::lastOsError();
#if defined(_WIN32)
    return error != 0 ? error : static_cast<int>(ERROR_GEN_FAILURE);
#else
    return error != 0 ? error : EIO;
#endif
}

std::uint64_t processId() noexcept {
#if defined(_WIN32)
    return GetCurrentProcessId();
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

// splitmix64 over a process-wide counter: distinct per call, seeded per process.
std::uint64_t nextRandom() noexcept {
    static std::atomic<std::uint64_t> state{
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
        (processId() << 32)};
    std::uint64_t z = state.fetch_add(0x9E3779B97F4A7C15ull) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Short, ASCII-only sibling name so long or non-ASCII target names can never
// push the temporary name over NAME_MAX or produce invalid UTF-8.
fs::path temporaryName(const char* tag) {
    char name[64];
    std::snprintf(name, sizeof name, ".pmm-%s-%llx-%016llx.tmp", tag,
                  static_cast<unsigned long long>(processId()),
                  static_cast<unsigned long long>(nextRandom()));
    return fs::path(name);
}

// Move-only owner of a native file handle. Methods return 0 on success or the
// OS error number, so callers can phrase the user-facing message.
class File {
public:
#if defined(_WIN32)
    using Handle = HANDLE;
    static Handle invalidHandle() noexcept { return INVALID_HANDLE_VALUE; }
#else
    using Handle = int;
    static Handle invalidHandle() noexcept { return -1; }
#endif

    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() { (void)close(); }

    int openForReading(const fs::path& path) noexcept {
        (void)close();
#if defined(_WIN32)
        m_handle = CreateFileW(
            path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        return m_handle == invalidHandle() ? failureCode() : 0;
#else
        do {
            m_handle = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        } while (m_handle < 0 && errno == EINTR);
        return m_handle < 0 ? failureCode() : 0;
#endif
    }

    // Creates a new file and fails if it exists. On POSIX the permission bits
    // are copied from `modeSource` when that is an existing regular file.
    int createNew(const fs::path& path, const fs::path& modeSource) noexcept {
        (void)close();
#if defined(_WIN32)
        (void)modeSource;
        m_handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        return m_handle == invalidHandle() ? failureCode() : 0;
#else
        struct stat existing{};
        mode_t mode = 0666;
        bool copyMode = false;
        if (!modeSource.empty() && ::stat(modeSource.c_str(), &existing) == 0 &&
            S_ISREG(existing.st_mode)) {
            mode = existing.st_mode & 07777;
            copyMode = true;
        }
        do {
            m_handle = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
        } while (m_handle < 0 && errno == EINTR);
        if (m_handle < 0)
            return failureCode();
        if (copyMode)
            (void)::fchmod(m_handle, mode); // open() applied the umask
        return 0;
#endif
    }

    int size(std::uint64_t& out) const noexcept {
#if defined(_WIN32)
        LARGE_INTEGER value{};
        if (!GetFileSizeEx(m_handle, &value))
            return failureCode();
        out = static_cast<std::uint64_t>(value.QuadPart);
#else
        struct stat st{};
        if (::fstat(m_handle, &st) != 0)
            return failureCode();
        out = static_cast<std::uint64_t>(st.st_size);
#endif
        return 0;
    }

    // Reads up to `capacity` bytes; `got` == 0 means end of file.
    int read(char* buffer, std::size_t capacity, std::size_t& got) noexcept {
        const std::size_t request = capacity < kMaxIoPerCall ? capacity : kMaxIoPerCall;
#if defined(_WIN32)
        DWORD transferred = 0;
        if (!ReadFile(m_handle, buffer, static_cast<DWORD>(request), &transferred, nullptr))
            return failureCode();
        got = transferred;
#else
        ssize_t transferred = 0;
        do {
            transferred = ::read(m_handle, buffer, request);
        } while (transferred < 0 && errno == EINTR);
        if (transferred < 0)
            return failureCode();
        got = static_cast<std::size_t>(transferred);
#endif
        return 0;
    }

    int writeAll(std::string_view data) noexcept {
        while (!data.empty()) {
            const std::size_t request = data.size() < kMaxIoPerCall ? data.size() : kMaxIoPerCall;
#if defined(_WIN32)
            DWORD written = 0;
            if (!WriteFile(m_handle, data.data(), static_cast<DWORD>(request), &written, nullptr))
                return failureCode();
            if (written == 0)
                return static_cast<int>(ERROR_WRITE_FAULT);
#else
            ssize_t written = 0;
            do {
                written = ::write(m_handle, data.data(), request);
            } while (written < 0 && errno == EINTR);
            if (written < 0)
                return failureCode();
            if (written == 0)
                return EIO;
#endif
            data.remove_prefix(static_cast<std::size_t>(written));
        }
        return 0;
    }

    // Flushes file data and metadata to stable storage.
    int sync() noexcept {
#if defined(_WIN32)
        return FlushFileBuffers(m_handle) ? 0 : failureCode();
#else
#if defined(__APPLE__)
        // fsync() on macOS only reaches the drive cache; F_FULLFSYNC flushes it.
        if (::fcntl(m_handle, F_FULLFSYNC) == 0)
            return 0;
#endif
        return ::fsync(m_handle) == 0 ? 0 : failureCode();
#endif
    }

    int close() noexcept {
        if (m_handle == invalidHandle())
            return 0;
#if defined(_WIN32)
        const bool ok = CloseHandle(m_handle) != 0;
#else
        // Never retry close() on EINTR: the descriptor is already released on Linux.
        const bool ok = ::close(m_handle) == 0 || errno == EINTR;
#endif
        const int error = ok ? 0 : failureCode();
        m_handle = invalidHandle();
        return error;
    }

private:
    Handle m_handle = invalidHandle();
};

void removeQuietly(const fs::path& path) noexcept {
#if defined(_WIN32)
    (void)DeleteFileW(path.c_str());
#else
    (void)::unlink(path.c_str());
#endif
}

#if defined(_WIN32)
bool hasReadOnlyAttribute(const fs::path& path) noexcept {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0;
}
#endif

// Renames `from` over `to`, replacing it atomically.
int replaceFile(const fs::path& from, const fs::path& to) noexcept {
#if defined(_WIN32)
    // Virus scanners and indexers briefly hold files open; retry a few times
    // unless the failure is clearly permanent (read-only target).
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return 0;
        const int error = failureCode();
        const bool transient = detail::isFileInUseError(error) ||
                               (error == ERROR_ACCESS_DENIED && !hasReadOnlyAttribute(to));
        if (!transient || attempt >= 4)
            return error;
        Sleep(static_cast<DWORD>(20u << attempt));
    }
#else
    return ::rename(from.c_str(), to.c_str()) == 0 ? 0 : failureCode();
#endif
}

// Shared implementation of writeFileAtomic and copyFileDurable: `produce`
// fills a fresh sibling temporary file, which is then synced and renamed over
// `path`. The temporary file never survives a failure.
Status writeAtomically(const fs::path& path, const std::function<Status(File&)>& produce) {
    if (path.empty() || !path.has_filename())
        return makeError(ErrorCode::InvalidArgument, "A file name is required.", pathToUtf8(path));
    const std::string name = quotedName(path);
    std::error_code ec;
    if (fs::is_directory(path, ec))
        return makeError(ErrorCode::IoError,
                         "Cannot write " + name + " because a folder with that name exists.",
                         pathToUtf8(path));
    const fs::path parent = path.parent_path();
    if (!parent.empty())
        PMM_TRY(createDirectories(parent));

    File file;
    fs::path temp;
    for (int attempt = 0;; ++attempt) {
        temp = parent / temporaryName("write");
        const int error = file.createNew(temp, path);
        if (error == 0)
            break;
        if (detail::classifyOsError(error, ErrorCode::IoError) != ErrorCode::AlreadyExists ||
            attempt >= 16)
            return osError(error, ErrorCode::IoError, "Could not write " + name + ".", temp);
    }

    Status result = produce(file);
    if (result.ok()) {
        if (const int error = file.sync())
            result = osError(error, ErrorCode::IoError, "Could not write " + name + ".", temp);
    }
    if (const int error = file.close(); error != 0 && result.ok())
        result = osError(error, ErrorCode::IoError, "Could not write " + name + ".", temp);
    if (!result.ok()) {
        removeQuietly(temp);
        return result;
    }
    if (const int error = replaceFile(temp, path)) {
        removeQuietly(temp);
        return osError(error, ErrorCode::IoError, "Could not replace " + name + ".", path);
    }
    detail::syncDirectoryBestEffort(parent);
    return {};
}

#if defined(_WIN32)
bool componentsEqual(const fs::path& a, const fs::path& b) noexcept {
    const auto& x = a.native();
    const auto& y = b.native();
    return CompareStringOrdinal(x.c_str(), static_cast<int>(x.size()), y.c_str(),
                                static_cast<int>(y.size()), TRUE) == CSTR_EQUAL;
}
#else
bool componentsEqual(const fs::path& a, const fs::path& b) noexcept {
    return a.native() == b.native();
}
#endif

} // namespace

Result<std::string> readFile(const fs::path& path, std::uint64_t maxBytes) {
    const std::string name = quotedName(path);
    std::error_code ec;
    if (fs::is_directory(path, ec))
        return makeError(ErrorCode::IoError, name + " is a folder, not a file.", pathToUtf8(path));

    File file;
    if (const int error = file.openForReading(path)) {
        if (detail::classifyOsError(error, ErrorCode::IoError) == ErrorCode::NotFound)
            return osError(error, ErrorCode::IoError, name + " does not exist.", path);
        return osError(error, ErrorCode::IoError, "Could not open " + name + ".", path);
    }
    const auto tooLarge = [&] {
        return makeError(ErrorCode::LimitExceeded,
                         name + " is larger than the allowed " + std::to_string(maxBytes) +
                             " bytes.",
                         pathToUtf8(path));
    };
    std::uint64_t size = 0;
    if (const int error = file.size(size))
        return osError(error, ErrorCode::IoError, "Could not read " + name + ".", path);
    if (size > maxBytes)
        return tooLarge();

    std::string contents;
    contents.reserve(static_cast<std::size_t>(size));
    std::vector<char> buffer(kChunkSize);
    for (;;) {
        std::size_t got = 0;
        if (const int error = file.read(buffer.data(), buffer.size(), got))
            return osError(error, ErrorCode::IoError, "Could not read " + name + ".", path);
        if (got == 0)
            break;
        // The file may grow while it is being read.
        if (contents.size() + got > maxBytes)
            return tooLarge();
        contents.append(buffer.data(), got);
    }
    return contents;
}

Status writeFileAtomic(const fs::path& path, std::string_view contents) {
    return writeAtomically(path, [&](File& file) -> Status {
        if (const int error = file.writeAll(contents))
            return osError(error, ErrorCode::IoError, "Could not write " + quotedName(path) + ".",
                           path);
        return {};
    });
}

Status copyFileDurable(const fs::path& source, const fs::path& destination) {
    const std::string sourceName = quotedName(source);
    std::error_code ec;
    const fs::file_status status = fs::status(source, ec);
    if (!fs::exists(status))
        return makeError(ErrorCode::NotFound, sourceName + " does not exist.", pathToUtf8(source));
    if (!fs::is_regular_file(status))
        return makeError(ErrorCode::IoError, sourceName + " is not a regular file.",
                         pathToUtf8(source));

    return writeAtomically(destination, [&](File& out) -> Status {
        // Opened here so the source is closed again before the rename.
        File in;
        if (const int error = in.openForReading(source))
            return osError(error, ErrorCode::IoError, "Could not open " + sourceName + ".", source);
        std::vector<char> buffer(kChunkSize);
        for (;;) {
            std::size_t got = 0;
            if (const int error = in.read(buffer.data(), buffer.size(), got))
                return osError(error, ErrorCode::IoError, "Could not read " + sourceName + ".",
                               source);
            if (got == 0)
                return {};
            if (const int error = out.writeAll(std::string_view(buffer.data(), got)))
                return osError(error, ErrorCode::IoError,
                               "Could not write " + quotedName(destination) + ".", destination);
        }
    });
}

Result<bool> isDirectoryWritable(const fs::path& directory) {
    const std::string name = quotedName(directory);
    std::error_code ec;
    const fs::file_status status = fs::status(directory, ec);
    if (!fs::exists(status))
        return makeError(ErrorCode::NotFound, "The folder " + name + " does not exist.",
                         pathToUtf8(directory));
    if (!fs::is_directory(status))
        return makeError(ErrorCode::InvalidArgument, name + " is not a folder.",
                         pathToUtf8(directory));

    for (int attempt = 0; attempt < 16; ++attempt) {
        const fs::path probe = directory / temporaryName("probe");
#if defined(_WIN32)
        const HANDLE handle = CreateFileW(
            probe.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            return true;
        }
#else
        const int fd = ::open(probe.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0) {
            (void)::close(fd);
            (void)::unlink(probe.c_str());
            return true;
        }
#endif
        const int error = failureCode();
        const ErrorCode code = detail::classifyOsError(error, ErrorCode::IoError);
        if (code == ErrorCode::PermissionDenied)
            return false;
        if (code != ErrorCode::AlreadyExists)
            return osError(error, ErrorCode::IoError,
                           "Could not check whether " + name + " is writable.", directory);
    }
    return makeError(ErrorCode::IoError, "Could not check whether " + name + " is writable.",
                     pathToUtf8(directory));
}

std::string pathToUtf8(const fs::path& path) {
#if defined(_WIN32)
    return detail::wideToUtf8(path.native());
#else
    return path.native();
#endif
}

fs::path pathFromUtf8(std::string_view utf8) {
#if defined(_WIN32)
    return fs::path(detail::utf8ToWide(utf8));
#else
    return fs::path(std::string(utf8));
#endif
}

std::string toGenericUtf8(const fs::path& path) {
#if defined(_WIN32)
    return detail::wideToUtf8(path.generic_wstring());
#else
    return path.generic_string();
#endif
}

std::int64_t lastWriteUnixSeconds(const fs::path& path) noexcept {
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return 0;
    ULARGE_INTEGER ticks{};
    ticks.LowPart = data.ftLastWriteTime.dwLowDateTime;
    ticks.HighPart = data.ftLastWriteTime.dwHighDateTime;
    // FILETIME counts 100 ns intervals since 1601-01-01.
    constexpr std::int64_t kUnixEpochTicks = 116444736000000000LL;
    const std::int64_t sinceEpoch = static_cast<std::int64_t>(ticks.QuadPart) - kUnixEpochTicks;
    return sinceEpoch >= 0 ? sinceEpoch / 10000000 : -((-sinceEpoch + 9999999) / 10000000);
#else
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0)
        return 0;
    return static_cast<std::int64_t>(st.st_mtime);
#endif
}

// A path is within itself. Comparison is case-insensitive on Windows.
bool isWithin(const fs::path& child, const fs::path& parent) {
    if (child.empty() || parent.empty())
        return false;
    std::error_code ec;
    const fs::path c = fs::absolute(child, ec).lexically_normal();
    if (ec)
        return false;
    const fs::path p = fs::absolute(parent, ec).lexically_normal();
    if (ec)
        return false;

    // Empty elements only appear for a trailing separator; ignore them.
    auto ci = c.begin();
    for (auto pi = p.begin(); pi != p.end(); ++pi) {
        if (pi->empty())
            continue;
        while (ci != c.end() && ci->empty())
            ++ci;
        if (ci == c.end() || !componentsEqual(*ci, *pi))
            return false;
        ++ci;
    }
    return true;
}

Status removeFile(const fs::path& path) {
    std::error_code ec;
    const fs::file_status status = fs::symlink_status(path, ec);
    if (status.type() == fs::file_type::not_found)
        return {};
    if (status.type() == fs::file_type::none)
        return detail::fsError(ec, ErrorCode::IoError, "Could not delete " + quotedName(path) + ".",
                               path);
    if (fs::is_directory(status))
        return makeError(ErrorCode::IoError,
                         "Cannot delete " + quotedName(path) + " because it is a folder.",
                         pathToUtf8(path));
#if defined(_WIN32)
    if (!DeleteFileW(path.c_str())) {
        const int error = failureCode();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return {};
        return osError(error, ErrorCode::IoError, "Could not delete " + quotedName(path) + ".",
                       path);
    }
#else
    if (::unlink(path.c_str()) != 0) {
        const int error = failureCode();
        if (error == ENOENT)
            return {};
        return osError(error, ErrorCode::IoError, "Could not delete " + quotedName(path) + ".",
                       path);
    }
    detail::syncDirectoryBestEffort(path.parent_path());
#endif
    return {};
}

Status createDirectories(const fs::path& directory) {
    std::error_code ec;
    if (fs::is_directory(directory, ec))
        return {};
    fs::create_directories(directory, ec);
    if (ec) {
        // Another process may have created it concurrently.
        std::error_code again;
        if (fs::is_directory(directory, again))
            return {};
        return detail::fsError(ec, ErrorCode::IoError,
                               "Could not create the folder " + quotedName(directory) + ".",
                               directory);
    }
    return {};
}

} // namespace pmm::platform
