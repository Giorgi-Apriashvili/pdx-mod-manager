#pragma once

// Conversions between the Qt-free layers (UTF-8 std::string, std::filesystem
// paths, pmm::Error) and Qt types.

#include "core/result.hpp"

#include <QString>
#include <QStringList>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::app {

inline QString qs(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

inline std::string utf8(const QString& text) {
    return text.toStdString(); // UTF-8 in Qt 6
}

QStringList qlist(const std::vector<std::string>& items);

// Path for display, with native separators.
QString qpath(const std::filesystem::path& path);
std::filesystem::path toPath(const QString& path);

// "source:id", the key InventoryModel and ModListModel use for identities.
QString identityKey(std::string_view source, std::string_view id);

} // namespace pmm::app
