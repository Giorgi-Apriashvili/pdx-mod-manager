#include "platform/process.hpp"

#include "platform/detail/os_util.hpp"
#include "platform/fs.hpp"

#include <windows.h>

#include <algorithm>
#include <system_error>
// Needs <windows.h> first.
#include <tlhelp32.h>

#include <cwchar>
#include <fstream>

namespace pmm::platform {
namespace {

using detail::quotedName;

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

// CreateProcessW pops up a modal "Unsupported 16-Bit Application" box for
// files that are not PE images, so the DOS header magic is checked first.
bool hasWindowsExecutableHeader(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    char magic[2] = {};
    return in.read(magic, sizeof magic) && magic[0] == 'M' && magic[1] == 'Z';
}

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
    if (!hasWindowsExecutableHeader(spec.executable))
        return makeError(ErrorCode::ProcessStartFailed,
                         quotedName(spec.executable) + " is not a Windows program.",
                         pathToUtf8(spec.executable));

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
    if (error == ERROR_ELEVATION_REQUIRED)
        message += " The program requires administrator rights.";
    else if (error == ERROR_BAD_EXE_FORMAT)
        message += " It is not a valid program for this system.";
    return Error(ErrorCode::ProcessStartFailed, std::move(message),
                 pathToUtf8(executable) + ": " + detail::osErrorText(error));
}

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

Result<StartedProcess> startDetached(const ProcessSpec& spec) {
    fs::path workingDirectory;
    PMM_TRY(validateSpec(spec, workingDirectory));

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
}

Result<std::vector<RunningProcess>> listProcesses() {
    std::vector<RunningProcess> processes;
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
    return processes;
}

bool executableNameMatches(const std::string& runningName, const std::string& wanted) {
    if (wanted.empty())
        return false;
    return asciiEqualsIgnoreCase(runningName, wanted);
}

std::int64_t currentProcessId() noexcept {
    return static_cast<std::int64_t>(GetCurrentProcessId());
}

} // namespace pmm::platform
