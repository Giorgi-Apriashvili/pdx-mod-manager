#include "platform/detail/os_util.hpp"

#include "platform/fs.hpp"

#include <windows.h>

namespace pmm::platform::detail {

int lastOsError() noexcept {
    return static_cast<int>(GetLastError());
}

std::string osErrorText(int error) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(error), 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::string text;
    if (length != 0 && buffer != nullptr)
        text = wideToUtf8(std::wstring_view(buffer, length));
    if (buffer != nullptr)
        LocalFree(buffer);
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' '))
        text.pop_back();
    if (text.empty())
        text = "Unknown error";
    return text + " (Windows error " + std::to_string(error) + ")";
}

ErrorCode classifyOsError(int error, ErrorCode fallback) noexcept {
    switch (static_cast<DWORD>(error)) {
    case ERROR_ACCESS_DENIED:
    case ERROR_WRITE_PROTECT:
    case ERROR_PRIVILEGE_NOT_HELD:
    case ERROR_ELEVATION_REQUIRED:
        return ErrorCode::PermissionDenied;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
        return ErrorCode::NotFound;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        return ErrorCode::AlreadyExists;
    default:
        return fallback;
    }
}

bool isFileInUseError(int error) noexcept {
    return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION ||
           error == ERROR_USER_MAPPED_FILE;
}

Error osError(int error, ErrorCode fallback, std::string message,
              const std::filesystem::path& path) {
    const ErrorCode code = classifyOsError(error, fallback);
    if (isFileInUseError(error))
        message += " The file is in use by another program; close it and try again.";
    else if (code == ErrorCode::PermissionDenied)
        message +=
            " Check that the location is not read-only and that you have permission to change it.";
    std::string detail = osErrorText(error);
    if (!path.empty())
        detail = pathToUtf8(path) + ": " + detail;
    return Error(code, std::move(message), std::move(detail));
}

Error fsError(const std::error_code& ec, ErrorCode fallback, std::string message,
              const std::filesystem::path& path) {
    // MSVC's std::filesystem reports Win32 error numbers in system_category.
    if (ec.category() == std::system_category())
        return osError(ec.value(), fallback, std::move(message), path);
    ErrorCode code = fallback;
    if (ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted ||
        ec == std::errc::read_only_file_system)
        code = ErrorCode::PermissionDenied;
    else if (ec == std::errc::no_such_file_or_directory)
        code = ErrorCode::NotFound;
    std::string detail = ec.message();
    if (!path.empty())
        detail = pathToUtf8(path) + ": " + detail;
    return Error(code, std::move(message), std::move(detail));
}

std::string quotedName(const std::filesystem::path& path) {
    const std::filesystem::path name = path.has_filename() ? path.filename() : path;
    return "\"" + toValidUtf8(pathToUtf8(name)) + "\"";
}

std::wstring utf8ToWide(std::string_view utf8) {
    if (utf8.empty())
        return {};
    // No MB_ERR_INVALID_CHARS: malformed input becomes U+FFFD instead of failing.
    const int needed =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(needed > 0 ? needed : 0), L'\0');
    if (needed > 0)
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(),
                            needed);
    return out;
}

std::string wideToUtf8(std::wstring_view wide) {
    if (wide.empty())
        return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(needed > 0 ? needed : 0), '\0');
    if (needed > 0)
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(),
                            needed, nullptr, nullptr);
    return out;
}

} // namespace pmm::platform::detail
