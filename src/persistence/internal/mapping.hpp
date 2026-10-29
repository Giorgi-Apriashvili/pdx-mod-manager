#pragma once

// Internal helpers shared by the repositories: conversions between domain
// values and their stored column representation. Not part of the public API.

#include "core/model.hpp"
#include "core/result.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Declares `var` from a Result-returning expression; returns its error on failure.
#define PMM_DB_TRY_VALUE(var, expr)                                                                \
    auto var = (expr);                                                                             \
    if (!var.ok())                                                                                 \
    return std::move(var).error()

namespace pmm::persistence::internal {

// SQL list of the non-terminal AcquisitionState values. Must match the partial
// unique index acquisition_jobs_one_unfinished (checked by a unit test).
inline constexpr const char* kUnfinishedJobStatesSql =
    "('pending', 'subscribing', 'subscribed', 'download_requested', 'downloading')";

std::optional<DistributionSource> distributionFromText(std::string_view text) noexcept;
std::optional<OsPlatform> platformFromText(std::string_view text) noexcept;
std::optional<InstallState> installStateFromText(std::string_view text) noexcept;

// Paths are stored as text in native format: the native bytes on POSIX, UTF-8
// on Windows. The conversions never throw. A Windows path that is not valid
// UTF-16 (an unpaired surrogate, which NTFS allows) has no UTF-8 form and is
// written with U+FFFD; ill-formed stored UTF-8 is read the same way. Only
// storable paths round-trip, so repositories reject the others on write.
std::string pathToText(const std::filesystem::path& path);
std::filesystem::path pathFromText(std::string_view text);
bool isStorablePath(const std::filesystem::path& path) noexcept;
// InvalidArgument naming `what` (e.g. "The mod folder") if `path` is not
// storable.
Status checkStorablePath(const std::filesystem::path& path, std::string_view what);

// JSON arrays of strings / [key, value] pairs. Invalid UTF-8 in the input is
// replaced with U+FFFD rather than failing the write.
std::string stringListToJson(const std::vector<std::string>& values);
Result<std::vector<std::string>> stringListFromJson(std::string_view json, std::string_view column);
std::string pairListToJson(const std::vector<std::pair<std::string, std::string>>& values);
Result<std::vector<std::pair<std::string, std::string>>> pairListFromJson(std::string_view json,
                                                                          std::string_view column);

// Error for a stored value that cannot be decoded (damaged or foreign data).
Error corruptValue(std::string_view what, std::string detail = {});

} // namespace pmm::persistence::internal
