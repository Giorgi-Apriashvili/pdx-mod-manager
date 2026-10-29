#pragma once

// Internal helpers shared by the platform implementation files: OS error text,
// mapping of OS error numbers onto pmm::ErrorCode, UTF-8 validation, and UTF-16
// conversions on Windows. Not part of the public platform API; other modules
// must not include this header.

#include "core/result.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace pmm::platform::detail {

// GetLastError() on Windows, errno elsewhere.
int lastOsError() noexcept;

// UTF-8 description of an OS error number, including the numeric code.
std::string osErrorText(int error);

// Access-denied class errors map to PermissionDenied, missing files/paths to
// NotFound, "exists" to AlreadyExists; anything else to `fallback`.
ErrorCode classifyOsError(int error, ErrorCode fallback) noexcept;

// True for Windows sharing/lock violations and POSIX EBUSY/ETXTBSY.
bool isFileInUseError(int error) noexcept;

// Builds an Error from an OS error number. `detail` becomes
// "<path>: <os text>"; `message` stays user-facing.
Error osError(int error, ErrorCode fallback, std::string message,
              const std::filesystem::path& path);

// Same for std::error_code values produced by std::filesystem.
Error fsError(const std::error_code& ec, ErrorCode fallback, std::string message,
              const std::filesystem::path& path);

// Quoted file name for user-facing messages, e.g. "\"dlc_load.json\"". Always
// valid UTF-8 (see toValidUtf8()).
std::string quotedName(const std::filesystem::path& path);

// True if `text` is well-formed UTF-8 (RFC 3629: no overlong forms, no
// surrogates, no code points above U+10FFFF, no truncated sequences).
bool isWellFormedUtf8(std::string_view text) noexcept;

// `text` with every byte that is not part of a well-formed sequence replaced
// by U+FFFD.
std::string toValidUtf8(std::string_view text);

// fsync() of a directory so that renames/creations inside it are durable.
// No-op on Windows (MOVEFILE_WRITE_THROUGH covers it). Best effort.
void syncDirectoryBestEffort(const std::filesystem::path& directory) noexcept;

#if defined(_WIN32)
std::wstring utf8ToWide(std::string_view utf8);
std::string wideToUtf8(std::wstring_view wide);
#endif

} // namespace pmm::platform::detail
