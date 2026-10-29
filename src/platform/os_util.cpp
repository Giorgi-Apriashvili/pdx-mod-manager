#include "platform/detail/os_util.hpp"

#include "platform/fs.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace pmm::platform::detail {
namespace {

// Length of the well-formed UTF-8 sequence starting at text[pos], or 0 if it
// is ill-formed.
std::size_t utf8SequenceLength(std::string_view text, std::size_t pos) noexcept {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(pos);
    if (lead < 0x80)
        return 1;

    std::size_t length = 0;
    unsigned char lo = 0x80; // allowed range of the second byte (excludes
    unsigned char hi = 0xBF; // overlongs, surrogates and > U+10FFFF)
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        if (lead == 0xE0)
            lo = 0xA0;
        else if (lead == 0xED)
            hi = 0x9F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        if (lead == 0xF0)
            lo = 0x90;
        else if (lead == 0xF4)
            hi = 0x8F;
    } else {
        return 0;
    }

    if (text.size() - pos < length || byte(pos + 1) < lo || byte(pos + 1) > hi)
        return 0;
    for (std::size_t i = 2; i < length; ++i) {
        if ((byte(pos + i) & 0xC0) != 0x80)
            return 0;
    }
    return length;
}

} // namespace

int lastOsError() noexcept {
#if defined(_WIN32)
    return static_cast<int>(GetLastError());
#else
    return errno;
#endif
}

std::string osErrorText(int error) {
#if defined(_WIN32)
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
#else
    return std::generic_category().message(error) + " (errno " + std::to_string(error) + ")";
#endif
}

ErrorCode classifyOsError(int error, ErrorCode fallback) noexcept {
#if defined(_WIN32)
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
#else
    switch (error) {
    case EACCES:
    case EPERM:
    case EROFS:
        return ErrorCode::PermissionDenied;
    case ENOENT:
    case ENOTDIR:
        return ErrorCode::NotFound;
    case EEXIST:
        return ErrorCode::AlreadyExists;
    default:
        return fallback;
    }
#endif
}

bool isFileInUseError(int error) noexcept {
#if defined(_WIN32)
    return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION ||
           error == ERROR_USER_MAPPED_FILE;
#else
    return error == EBUSY || error == ETXTBSY;
#endif
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
#if defined(_WIN32)
    // MSVC's std::filesystem reports Win32 error numbers in system_category.
    if (ec.category() == std::system_category())
        return osError(ec.value(), fallback, std::move(message), path);
#else
    if (ec.category() == std::system_category() || ec.category() == std::generic_category())
        return osError(ec.value(), fallback, std::move(message), path);
#endif
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

bool isWellFormedUtf8(std::string_view text) noexcept {
    for (std::size_t pos = 0; pos < text.size();) {
        const std::size_t length = utf8SequenceLength(text, pos);
        if (length == 0)
            return false;
        pos += length;
    }
    return true;
}

std::string toValidUtf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t pos = 0; pos < text.size();) {
        const std::size_t length = utf8SequenceLength(text, pos);
        if (length == 0) {
            out += "\xEF\xBF\xBD";
            ++pos;
        } else {
            out.append(text.substr(pos, length));
            pos += length;
        }
    }
    return out;
}

void syncDirectoryBestEffort(const std::filesystem::path& directory) noexcept {
#if defined(_WIN32)
    (void)directory;
#else
    const std::string native = directory.empty() ? std::string(".") : directory.native();
    int flags = O_RDONLY | O_CLOEXEC;
#if defined(O_DIRECTORY)
    flags |= O_DIRECTORY;
#endif
    const int fd = ::open(native.c_str(), flags);
    if (fd < 0)
        return;
    // Some filesystems reject fsync on directories (EINVAL); durability is then
    // whatever the filesystem provides.
    (void)::fsync(fd);
    (void)::close(fd);
#endif
}

#if defined(_WIN32)
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
#endif

} // namespace pmm::platform::detail
