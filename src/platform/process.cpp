#include "platform/process.hpp"

#include "platform/detail/os_util.hpp"
#include "platform/fs.hpp"

#include <algorithm>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
// Needs <windows.h> first.
#include <tlhelp32.h>

#include <cwchar>
#include <fstream>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <csignal>
#include <fstream>
#include <thread>
#if defined(__APPLE__)
#include <crt_externs.h>
#include <libproc.h>
#include <sys/param.h>
#endif
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
extern char** environ;
#endif

// posix_spawn_file_actions_addchdir_np: glibc 2.29+, macOS 10.15+ (checked at
// run time there). Elsewhere startDetached falls back to fork/chdir/execve.
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29))
#define PMM_HAVE_SPAWN_ADDCHDIR 1
#elif defined(__APPLE__)
#define PMM_HAVE_SPAWN_ADDCHDIR 1
#else
#define PMM_HAVE_SPAWN_ADDCHDIR 0
#endif

#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
#define PMM_HAVE_SPAWN_CLOSEFROM 1
#else
#define PMM_HAVE_SPAWN_CLOSEFROM 0
#endif

namespace pmm::platform {
namespace {

using detail::quotedName;

#if defined(_WIN32) || defined(__APPLE__)
bool asciiEqualsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto lower = [](char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        };
        if (lower(a[i]) != lower(b[i]))
            return false;
    }
    return true;
}
#endif

#if defined(_WIN32)
// CreateProcessW pops up a modal "Unsupported 16-Bit Application" box for
// files that are not PE images, so the DOS header magic is checked first.
bool hasWindowsExecutableHeader(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    char magic[2] = {};
    return in.read(magic, sizeof magic) && magic[0] == 'M' && magic[1] == 'Z';
}
#endif

// Checks the spec and resolves the effective working directory.
Status validateSpec(const ProcessSpec& spec, fs::path& workingDirectory) {
    if (spec.executable.empty() || !spec.executable.is_absolute())
        return makeError(ErrorCode::InvalidArgument, "The program path must be absolute.",
                         pathToUtf8(spec.executable));
    std::error_code ec;
    const fs::file_status status = fs::status(spec.executable, ec);
    if (!fs::exists(status))
        return makeError(ErrorCode::ProcessStartFailed,
                         "The program " + quotedName(spec.executable) + " does not exist.",
                         pathToUtf8(spec.executable));
    if (!fs::is_regular_file(status))
        return makeError(ErrorCode::ProcessStartFailed,
                         quotedName(spec.executable) + " is not a program file.",
                         pathToUtf8(spec.executable));
#if defined(_WIN32)
    if (!hasWindowsExecutableHeader(spec.executable))
        return makeError(ErrorCode::ProcessStartFailed,
                         quotedName(spec.executable) + " is not a Windows program.",
                         pathToUtf8(spec.executable));
#endif

    workingDirectory =
        spec.workingDirectory.empty() ? spec.executable.parent_path() : spec.workingDirectory;
    if (!fs::is_directory(workingDirectory, ec))
        return makeError(ErrorCode::ProcessStartFailed,
                         "The working folder " + quotedName(workingDirectory) + " does not exist.",
                         pathToUtf8(workingDirectory));

    for (const std::string& argument : spec.arguments) {
        if (argument.find('\0') != std::string::npos)
            return makeError(ErrorCode::InvalidArgument,
                             "Program arguments must not contain NUL characters.");
    }
    for (const auto& [name, value] : spec.environment) {
        if (name.empty() || name.find_first_of(std::string_view("=\0", 2)) != std::string::npos ||
            value.find('\0') != std::string::npos)
            return makeError(ErrorCode::InvalidArgument,
                             "Invalid environment variable for the program.", name);
    }
    return {};
}

Error startError(int error, const fs::path& executable) {
    std::string message = "Could not start " + quotedName(executable) + ".";
#if defined(_WIN32)
    if (error == ERROR_ELEVATION_REQUIRED)
        message += " The program requires administrator rights.";
    else if (error == ERROR_BAD_EXE_FORMAT)
        message += " It is not a valid program for this system.";
#else
    if (error == EACCES)
        message += " The file is not executable or you lack permission to run it.";
    else if (error == ENOEXEC)
        message += " It is not a valid program for this system.";
#endif
    return Error(ErrorCode::ProcessStartFailed, std::move(message),
                 pathToUtf8(executable) + ": " + detail::osErrorText(error));
}

#if defined(_WIN32)

int compareEnvironmentNames(std::wstring_view a, std::wstring_view b) noexcept {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                static_cast<int>(b.size()), TRUE);
}

// Variable name of a "NAME=value" entry. Hidden per-drive entries such as
// "=C:=C:\dir" start with '=', so the separator search starts at index 1.
std::wstring_view environmentEntryName(std::wstring_view entry) noexcept {
    const std::size_t separator = entry.find(L'=', 1);
    return separator == std::wstring_view::npos ? entry : entry.substr(0, separator);
}

// Inherited environment merged with `additions`, sorted case-insensitively by
// name as CreateProcessW requires, double-NUL terminated.
std::wstring
buildEnvironmentBlock(const std::vector<std::pair<std::string, std::string>>& additions) {
    std::vector<std::wstring> entries;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* entry = block; *entry != L'\0'; entry += std::wcslen(entry) + 1)
            entries.emplace_back(entry);
        FreeEnvironmentStringsW(block);
    }
    for (const auto& [name, value] : additions) {
        const std::wstring wideName = detail::utf8ToWide(name);
        std::wstring entry = wideName + L"=" + detail::utf8ToWide(value);
        const auto existing =
            std::find_if(entries.begin(), entries.end(), [&](const std::wstring& e) {
                return compareEnvironmentNames(environmentEntryName(e), wideName) == CSTR_EQUAL;
            });
        if (existing != entries.end())
            *existing = std::move(entry);
        else
            entries.push_back(std::move(entry));
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const std::wstring& a, const std::wstring& b) {
                         return compareEnvironmentNames(environmentEntryName(a),
                                                        environmentEntryName(b)) == CSTR_LESS_THAN;
                     });
    std::wstring block;
    for (const std::wstring& entry : entries) {
        block += entry;
        block += L'\0';
    }
    if (entries.empty())
        block += L'\0';
    block += L'\0';
    return block;
}

#else

char** inheritedEnvironment() noexcept {
#if defined(__APPLE__)
    return *_NSGetEnviron();
#else
    return environ;
#endif
}

std::vector<std::string>
mergedEnvironment(const std::vector<std::pair<std::string, std::string>>& additions) {
    std::vector<std::string> entries;
    for (char** entry = inheritedEnvironment(); entry != nullptr && *entry != nullptr; ++entry)
        entries.emplace_back(*entry);
    for (const auto& [name, value] : additions) {
        std::string entry = name + "=" + value;
        const auto existing =
            std::find_if(entries.begin(), entries.end(), [&](const std::string& e) {
                return e.size() > name.size() && e.compare(0, name.size(), name) == 0 &&
                       e[name.size()] == '=';
            });
        if (existing != entries.end())
            *existing = std::move(entry);
        else
            entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<char*> pointerArray(std::vector<std::string>& strings) {
    std::vector<char*> pointers;
    pointers.reserve(strings.size() + 1);
    for (std::string& s : strings)
        pointers.push_back(s.data());
    pointers.push_back(nullptr);
    return pointers;
}

// Reaps the child when it exits so it never stays a zombie while we run.
void reapInBackground(pid_t pid) {
    try {
        std::thread([pid] {
            int status = 0;
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
        }).detach();
    } catch (const std::system_error&) {
        // No thread available: the exited child lingers as a zombie until we exit.
    }
}

// Fallback when posix_spawn cannot change the working directory. The child
// only calls async-signal-safe functions; exec failures travel back through a
// close-on-exec pipe.
Result<StartedProcess> forkExec(const fs::path& executable, const fs::path& workingDirectory,
                                char* const* argv, char* const* envp) {
    int pipeFds[2];
    if (::pipe(pipeFds) != 0)
        return startError(errno, executable);
    (void)::fcntl(pipeFds[0], F_SETFD, FD_CLOEXEC);
    (void)::fcntl(pipeFds[1], F_SETFD, FD_CLOEXEC);
    const char* exePath = executable.c_str();
    const char* cwdPath = workingDirectory.c_str();

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int error = errno;
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        return startError(error, executable);
    }
    if (pid == 0) {
        ::close(pipeFds[0]);
        ::setsid();
        sigset_t mask;
        (void)sigemptyset(&mask);
        ::sigprocmask(SIG_SETMASK, &mask, nullptr);
        struct sigaction defaultAction{};
        defaultAction.sa_handler = SIG_DFL;
        ::sigaction(SIGPIPE, &defaultAction, nullptr);
        int error = 0;
        if (::chdir(cwdPath) != 0) {
            error = errno;
        } else {
            const int devNull = ::open("/dev/null", O_RDWR);
            if (devNull >= 0) {
                ::dup2(devNull, STDIN_FILENO);
                ::dup2(devNull, STDOUT_FILENO);
                ::dup2(devNull, STDERR_FILENO);
                if (devNull > STDERR_FILENO)
                    ::close(devNull);
            }
            ::execve(exePath, argv, envp);
            error = errno;
        }
        (void)!::write(pipeFds[1], &error, sizeof error);
        ::_exit(127);
    }

    ::close(pipeFds[1]);
    int childError = 0;
    ssize_t got = 0;
    do {
        got = ::read(pipeFds[0], &childError, sizeof childError);
    } while (got < 0 && errno == EINTR);
    ::close(pipeFds[0]);
    if (got == static_cast<ssize_t>(sizeof childError)) {
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        return startError(childError, executable);
    }
    reapInBackground(pid);
    return StartedProcess{static_cast<std::int64_t>(pid)};
}

#if !defined(__APPLE__)
std::string readFirstLine(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string line;
    std::getline(in, line);
    return line;
}

std::string readFirstNulTerminated(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string value;
    std::getline(in, value, '\0');
    return value;
}
#endif

#endif

} // namespace

std::wstring quoteWindowsArgument(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return argument;
    // Backslashes are literal unless they precede a quote; then each one must be
    // doubled, plus one more to escape the quote itself. Trailing backslashes are
    // doubled because the closing quote follows them.
    std::wstring quoted;
    quoted.reserve(argument.size() + 2);
    quoted.push_back(L'"');
    for (auto it = argument.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != argument.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == argument.end()) {
            quoted.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(*it);
        }
    }
    quoted.push_back(L'"');
    return quoted;
}

#if defined(_WIN32)
std::wstring buildWindowsCommandLine(const fs::path& executable,
                                     const std::vector<std::string>& arguments) {
    // argv[0] is parsed without backslash escapes, and Windows paths cannot
    // contain quotes, so plain quoting is exact for it.
    std::wstring line = L"\"" + executable.native() + L"\"";
    for (const std::string& argument : arguments) {
        line += L' ';
        line += quoteWindowsArgument(detail::utf8ToWide(argument));
    }
    return line;
}
#endif

Result<StartedProcess> startDetached(const ProcessSpec& spec) {
    fs::path workingDirectory;
    PMM_TRY(validateSpec(spec, workingDirectory));

#if defined(_WIN32)
    std::wstring commandLine = buildWindowsCommandLine(spec.executable, spec.arguments);
    if (commandLine.size() >= 32767)
        return makeError(ErrorCode::InvalidArgument,
                         "The program arguments exceed the Windows command-line limit.");
    std::wstring environment;
    if (!spec.environment.empty())
        environment = buildEnvironmentBlock(spec.environment);

    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION info{};
    // The child gets the default error mode rather than inheriting ours.
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS |
                  CREATE_DEFAULT_ERROR_MODE;
    // A job object with "kill on close" would take the game down with us.
    BOOL inJob = FALSE;
    if (IsProcessInJob(GetCurrentProcess(), nullptr, &inJob) && inJob)
        flags |= CREATE_BREAKAWAY_FROM_JOB;
    const auto create = [&](DWORD creationFlags) {
        return CreateProcessW(spec.executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                              creationFlags, environment.empty() ? nullptr : environment.data(),
                              workingDirectory.c_str(), &startup, &info);
    };
    // A damaged or foreign executable would otherwise raise a modal "Bad
    // Image" system dialog that blocks this call until it is dismissed or
    // times out; the failure is reported through the returned Error instead.
    DWORD previousErrorMode = 0;
    const bool errorModeChanged =
        SetThreadErrorMode(GetThreadErrorMode() | SEM_FAILCRITICALERRORS, &previousErrorMode) != 0;
    BOOL created = create(flags);
    int error = created ? 0 : detail::lastOsError();
    if (!created && (flags & CREATE_BREAKAWAY_FROM_JOB) != 0 && error == ERROR_ACCESS_DENIED) {
        created = create(flags & ~static_cast<DWORD>(CREATE_BREAKAWAY_FROM_JOB));
        error = created ? 0 : detail::lastOsError();
    }
    if (errorModeChanged)
        SetThreadErrorMode(previousErrorMode, nullptr);
    if (!created)
        return startError(error, spec.executable);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return StartedProcess{static_cast<std::int64_t>(info.dwProcessId)};
#else
    std::vector<std::string> argvStorage;
    argvStorage.reserve(spec.arguments.size() + 1);
    argvStorage.push_back(spec.executable.native());
    argvStorage.insert(argvStorage.end(), spec.arguments.begin(), spec.arguments.end());
    std::vector<std::string> envStorage = mergedEnvironment(spec.environment);
    std::vector<char*> argv = pointerArray(argvStorage);
    std::vector<char*> envp = pointerArray(envStorage);

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0)
        return startError(ENOMEM, spec.executable);
    bool chdirQueued = false;
#if PMM_HAVE_SPAWN_ADDCHDIR
#if defined(__APPLE__)
    if (__builtin_available(macOS 10.15, *))
        chdirQueued = posix_spawn_file_actions_addchdir_np(&actions, workingDirectory.c_str()) == 0;
#else
    chdirQueued = posix_spawn_file_actions_addchdir_np(&actions, workingDirectory.c_str()) == 0;
#endif
#endif
    if (!chdirQueued) {
        posix_spawn_file_actions_destroy(&actions);
        return forkExec(spec.executable, workingDirectory, argv.data(), envp.data());
    }
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
#if PMM_HAVE_SPAWN_CLOSEFROM
    posix_spawn_file_actions_addclosefrom_np(&actions, STDERR_FILENO + 1);
#endif

    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return startError(ENOMEM, spec.executable);
    }
    int flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    // A new session detaches the child from our terminal and process group, so
    // closing the manager (or its terminal) does not signal the game.
#if defined(POSIX_SPAWN_SETSID)
    flags |= POSIX_SPAWN_SETSID;
#else
    flags |= POSIX_SPAWN_SETPGROUP;
    posix_spawnattr_setpgroup(&attributes, 0);
#endif
#if defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT; // macOS: inherit only the stdio set up above
#endif
    sigset_t mask;
    (void)sigemptyset(&mask);
    posix_spawnattr_setsigmask(&attributes, &mask);
    sigset_t defaults;
    (void)sigemptyset(&defaults);
    (void)sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    posix_spawnattr_setflags(&attributes, static_cast<short>(flags));

    pid_t pid = 0;
    const int rc =
        posix_spawn(&pid, spec.executable.c_str(), &actions, &attributes, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (rc != 0)
        return startError(rc, spec.executable);
    reapInBackground(pid);
    return StartedProcess{static_cast<std::int64_t>(pid)};
#endif
}

Result<std::vector<RunningProcess>> listProcesses() {
    std::vector<RunningProcess> processes;
#if defined(_WIN32)
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return detail::osError(detail::lastOsError(), ErrorCode::IoError,
                               "Could not list running programs.", {});
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    std::wstring imagePath(32768, L'\0');
    if (Process32FirstW(snapshot, &entry)) {
        do {
            RunningProcess process;
            process.pid = static_cast<std::int64_t>(entry.th32ProcessID);
            process.executableName = detail::wideToUtf8(entry.szExeFile);
            if (entry.th32ProcessID != 0) {
                // Protected and other users' processes refuse this; the path stays empty.
                if (const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                                      entry.th32ProcessID)) {
                    DWORD size = static_cast<DWORD>(imagePath.size());
                    if (QueryFullProcessImageNameW(handle, 0, imagePath.data(), &size))
                        process.executablePath = fs::path(imagePath.substr(0, size));
                    CloseHandle(handle);
                }
            }
            processes.push_back(std::move(process));
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
#elif defined(__APPLE__)
    const int count = proc_listallpids(nullptr, 0);
    if (count <= 0)
        return detail::osError(errno, ErrorCode::IoError, "Could not list running programs.", {});
    std::vector<pid_t> pids(static_cast<std::size_t>(count) + 64);
    const int filled = proc_listallpids(pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    if (filled <= 0)
        return detail::osError(errno, ErrorCode::IoError, "Could not list running programs.", {});
    pids.resize(std::min(pids.size(), static_cast<std::size_t>(filled)));
    std::vector<char> path(PROC_PIDPATHINFO_MAXSIZE);
    for (const pid_t pid : pids) {
        if (pid <= 0)
            continue;
        RunningProcess process;
        process.pid = pid;
        const int length = proc_pidpath(pid, path.data(), static_cast<std::uint32_t>(path.size()));
        if (length > 0) {
            process.executablePath =
                fs::path(std::string(path.data(), static_cast<std::size_t>(length)));
            process.executableName = process.executablePath.filename().string();
        } else {
            char name[2 * MAXCOMLEN + 1] = {};
            if (proc_name(pid, name, sizeof name) > 0)
                process.executableName = name;
        }
        processes.push_back(std::move(process));
    }
#else
    std::error_code ec;
    fs::directory_iterator it("/proc", ec);
    if (ec)
        return detail::fsError(ec, ErrorCode::IoError, "Could not list running programs.", "/proc");
    for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::string name = it->path().filename().string();
        long long pid = 0;
        const auto [end, parseError] = std::from_chars(name.data(), name.data() + name.size(), pid);
        if (name.empty() || parseError != std::errc() || end != name.data() + name.size())
            continue;
        RunningProcess process;
        process.pid = pid;
        // /proc/<pid>/exe is only readable for our own processes.
        std::error_code linkError;
        const fs::path exe = fs::read_symlink(it->path() / "exe", linkError);
        if (!linkError && !exe.empty()) {
            std::string native = exe.native();
            constexpr std::string_view kDeleted = " (deleted)";
            if (native.size() > kDeleted.size() && native.ends_with(kDeleted))
                native.resize(native.size() - kDeleted.size());
            process.executablePath = fs::path(native);
            process.executableName = process.executablePath.filename().string();
        }
        if (process.executableName.empty()) {
            std::string comm = readFirstLine(it->path() / "comm");
            // comm is truncated to 15 bytes; argv[0] has the full name when it agrees.
            const std::string argv0 =
                fs::path(readFirstNulTerminated(it->path() / "cmdline")).filename().string();
            if (comm.size() >= 15 && argv0.size() > comm.size() &&
                argv0.compare(0, comm.size(), comm) == 0)
                comm = argv0;
            process.executableName = std::move(comm);
        }
        processes.push_back(std::move(process));
    }
#endif
    return processes;
}

bool executableNameMatches(const std::string& runningName, const std::string& wanted) {
    if (wanted.empty())
        return false;
#if defined(_WIN32) || defined(__APPLE__)
    return asciiEqualsIgnoreCase(runningName, wanted);
#else
    return runningName == wanted;
#endif
}

std::int64_t currentProcessId() noexcept {
#if defined(_WIN32)
    return static_cast<std::int64_t>(GetCurrentProcessId());
#else
    return static_cast<std::int64_t>(::getpid());
#endif
}

} // namespace pmm::platform
