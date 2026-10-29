#include "app/core/qt_util.hpp"

#include "platform/fs.hpp"

#include <QDir>

namespace pmm::app {

QStringList qlist(const std::vector<std::string>& items) {
    QStringList out;
    out.reserve(static_cast<qsizetype>(items.size()));
    for (const std::string& item : items)
        out.push_back(qs(item));
    return out;
}

QString qpath(const std::filesystem::path& path) {
    if (path.empty())
        return {};
    return QDir::toNativeSeparators(qs(platform::pathToUtf8(path)));
}

std::filesystem::path toPath(const QString& path) {
    return platform::pathFromUtf8(utf8(path));
}

QString identityKey(std::string_view source, std::string_view id) {
    return qs(source) + QLatin1Char(':') + qs(id);
}

} // namespace pmm::app
