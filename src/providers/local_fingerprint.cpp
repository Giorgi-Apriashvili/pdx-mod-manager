#include "providers/local_fingerprint.hpp"

#include "core/sha256.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <vector>

namespace pmm::providers {
namespace {

namespace fs = std::filesystem;

std::string utf8(const fs::path& p, bool generic) {
    const auto u8 = generic ? p.generic_u8string() : p.u8string();
    return std::string(u8.begin(), u8.end());
}

bool equalsAsciiNoCase(std::string_view a, std::string_view b) noexcept {
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

bool isJunkDirectoryName(std::string_view name) noexcept {
    return name == ".git" || name == "__MACOSX";
}

bool isJunkFileName(std::string_view name) noexcept {
    return equalsAsciiNoCase(name, ".DS_Store") || equalsAsciiNoCase(name, "Thumbs.db") ||
           equalsAsciiNoCase(name, "desktop.ini") || name.starts_with("._");
}

bool isCancelled(const FingerprintOptions& options) noexcept {
    return options.cancel != nullptr && options.cancel->load(std::memory_order_relaxed);
}

Error cancelledError() {
    return makeError(ErrorCode::Cancelled, "Fingerprinting was cancelled.");
}

Error fsError(const std::error_code& ec, std::string_view what, const fs::path& path) {
    const ErrorCode code =
        ec == std::errc::permission_denied ? ErrorCode::PermissionDenied : ErrorCode::IoError;
    return makeError(code, std::string(what), utf8(path, false) + ": " + ec.message());
}

struct FileEntry {
    std::string key;
    fs::path path;
};

} // namespace

bool isFingerprintJunk(std::string_view genericRelativePath) noexcept {
    std::string_view rest = genericRelativePath;
    while (!rest.empty()) {
        const std::size_t slash = rest.find('/');
        const std::string_view part = rest.substr(0, slash);
        if (isJunkDirectoryName(part))
            return true;
        if (slash == std::string_view::npos)
            return isJunkFileName(part);
        rest.remove_prefix(slash + 1);
    }
    return false;
}

Result<std::string> fingerprintDirectory(const fs::path& root, const FingerprintOptions& options) {
    fs::path base = root.lexically_normal();
    if (!base.has_filename() && base.has_relative_path())
        base = base.parent_path(); // drop a trailing separator so relative paths are clean

    std::error_code ec;
    const auto rootStatus = fs::status(base, ec);
    if (ec || !fs::exists(rootStatus)) {
        return makeError(ErrorCode::NotFound, "The mod folder does not exist.", utf8(root, false));
    }
    if (!fs::is_directory(rootStatus)) {
        return makeError(ErrorCode::InvalidArgument, "The mod location is not a folder.",
                         utf8(root, false));
    }

    std::vector<FileEntry> files;
    std::uint64_t totalBytes = 0;
    fs::recursive_directory_iterator it(base, fs::directory_options::none, ec);
    if (ec)
        return fsError(ec, "Cannot read the mod folder.", base);
    for (const fs::recursive_directory_iterator end; it != end; it.increment(ec)) {
        if (ec)
            return fsError(ec, "Cannot list the mod folder.", base);
        if (isCancelled(options))
            return cancelledError();
        const fs::directory_entry& entry = *it;
        std::error_code typeEc;
        if (entry.is_directory(typeEc)) {
            // Symlinked directories report true here but are never recursed into.
            if (isJunkDirectoryName(utf8(entry.path().filename(), true)))
                it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(typeEc))
            continue; // sockets, broken symlinks, ...
        std::string key = utf8(entry.path().lexically_relative(base), true);
        if (isFingerprintJunk(key))
            continue;
        const std::uintmax_t size = entry.file_size(typeEc);
        if (typeEc)
            return fsError(typeEc, "Cannot read a file in the mod folder.", entry.path());
        totalBytes += size;
        if (totalBytes > options.maxTotalBytes) {
            return makeError(ErrorCode::LimitExceeded,
                             "The mod folder is too large to fingerprint.",
                             "limit " + std::to_string(options.maxTotalBytes) + " bytes");
        }
        if (files.size() >= options.maxFiles) {
            return makeError(ErrorCode::LimitExceeded,
                             "The mod folder contains too many files to fingerprint.",
                             "limit " + std::to_string(options.maxFiles) + " files");
        }
        files.push_back(FileEntry{std::move(key), entry.path()});
    }
    if (ec)
        return fsError(ec, "Cannot list the mod folder.", base);

    std::sort(files.begin(), files.end(),
              [](const FileEntry& a, const FileEntry& b) { return a.key < b.key; });

    Sha256 outer;
    std::vector<char> buffer(256 * 1024);
    for (const FileEntry& file : files) {
        if (isCancelled(options))
            return cancelledError();
        std::ifstream in(file.path, std::ios::binary);
        if (!in) {
            return makeError(ErrorCode::IoError, "Cannot open a file in the mod folder.",
                             utf8(file.path, false));
        }
        Sha256 inner;
        std::uint64_t size = 0;
        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto got = static_cast<std::size_t>(in.gcount());
            if (got == 0)
                break;
            inner.update(std::string_view(buffer.data(), got));
            size += got;
            if (isCancelled(options))
                return cancelledError();
        }
        if (in.bad()) {
            return makeError(ErrorCode::IoError, "Cannot read a file in the mod folder.",
                             utf8(file.path, false));
        }
        outer.update(file.key);
        outer.update(std::string_view("\0", 1));
        outer.update(std::to_string(size));
        outer.update(std::string_view("\0", 1));
        outer.update(Sha256::toHex(inner.finish()));
        outer.update(std::string_view("\n", 1));
    }
    return "sha256:" + Sha256::toHex(outer.finish());
}

} // namespace pmm::providers
