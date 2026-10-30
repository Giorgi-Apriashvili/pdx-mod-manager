#pragma once

// Process creation and inspection through native OS APIs. Arguments are always
// passed as a vector and quoted for the CreateProcessW command line; nothing is
// ever routed through a shell.

#include "core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace pmm::platform {

namespace fs = std::filesystem;

struct ProcessSpec {
    fs::path executable; // absolute path; must exist and be a regular file
    fs::path workingDirectory;
    std::vector<std::string> arguments;                           // argv[1..], UTF-8
    std::vector<std::pair<std::string, std::string>> environment; // added to the inherited env
};

struct StartedProcess {
    std::int64_t pid = 0;
};

// Starts a detached process (does not wait, does not inherit handles/pipes).
// Fails with ProcessStartFailed including the OS error text in `detail`.
Result<StartedProcess> startDetached(const ProcessSpec& spec);

// Windows command-line quoting for one argument (CommandLineToArgvW rules).
// Exposed for tests.
std::wstring quoteWindowsArgument(const std::wstring& argument);

struct RunningProcess {
    std::int64_t pid = 0;
    std::string executableName; // file name only, e.g. "stellaris.exe"
    fs::path executablePath;    // empty if not accessible
};

// Snapshot of running processes visible to the current user.
Result<std::vector<RunningProcess>> listProcesses();

// ASCII case-insensitive.
bool executableNameMatches(const std::string& runningName, const std::string& wanted);

// Notes on startDetached():
// - An empty workingDirectory means the executable's folder.
// - InvalidArgument for a relative executable path, NUL characters in
//   arguments/environment, or environment names that are empty or contain '=';
//   ProcessStartFailed for everything else (missing executable or working
//   folder, OS refusal).
// - CreateProcessW with bInheritHandles=FALSE and
//   DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_DEFAULT_ERROR_MODE,
//   breaking away from an enclosing job object when the job allows it. The
//   environment block is the inherited one merged with `environment`, sorted
//   case-insensitively. System "bad image" dialogs are suppressed for the
//   call; such failures come back as ProcessStartFailed.

// Full command line for `executable` + UTF-8 `arguments`, exactly as passed to
// CreateProcessW. Exposed for tests.
std::wstring buildWindowsCommandLine(const fs::path& executable,
                                     const std::vector<std::string>& arguments);

std::int64_t currentProcessId() noexcept;

} // namespace pmm::platform
