// pmm-icon-gen: renders the SVG sources of the application icon and writes the
// files the build uses (docs/packaging.md, "Application icon").
//
//   pmm-icon-gen <icons dir> <windows packaging dir>
//
// Each size comes from <icons dir>/app-<size>.svg when that file exists (hand-hinted
// art for sizes where app.svg does not land on whole pixels), else from app.svg.
//
// Writes <windows packaging dir>/app.ico, the wizard images in
// <windows packaging dir>/wizard-small/ and the README's <icons dir>/app-128.png.

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QImage>
#include <QPainter>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QSvgRenderer>

#include <array>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

// What Windows picks from for the small and large shell icons at each display scale.
constexpr std::array kIconSizes{16, 20, 24, 32, 40, 48, 64, 256};
// The icon format's entry of this size is stored as PNG, smaller ones as DIB.
constexpr int kPngEntrySize = 256;
// The wizard's upper right image area at 100% to 250% (Inno Setup's help).
constexpr std::array kWizardSizes{58, 77, 97, 116, 124, 143, 159};
constexpr int kReadmeSize = 128;

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

QImage render(const QString& iconsDir, int size) {
    const QDir dir(iconsDir);
    const QString tuned = dir.filePath(QStringLiteral("app-%1.svg").arg(size));
    const QString path = QFile::exists(tuned) ? tuned : dir.filePath(QStringLiteral("app.svg"));
    QSvgRenderer renderer(path);
    if (!renderer.isValid())
        fail(QStringLiteral("not a valid SVG: %1").arg(path));

    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, size, size));
    painter.end();
    // The icon and PNG formats store straight alpha.
    return image.convertToFormat(QImage::Format_ARGB32);
}

QByteArray pngBytes(const QImage& image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        fail(QStringLiteral("could not encode a %1 px PNG").arg(image.width()));
    return bytes;
}

// BITMAPINFOHEADER, bottom-up BGRA rows, then the 1-bit AND mask (rows padded to
// 32 bits, 1 = transparent), which the format requires even with an alpha channel.
QByteArray dibBytes(const QImage& image) {
    const int size = image.width();
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out << quint32(40) << qint32(size) << qint32(size * 2) // the height counts both bitmaps
        << quint16(1) << quint16(32) << quint32(0) << quint32(0) << qint32(0) << qint32(0)
        << quint32(0) << quint32(0);

    for (int y = size - 1; y >= 0; --y) {
        for (int x = 0; x < size; ++x)
            out << quint32(image.pixel(x, y)); // 0xAARRGGBB, so little-endian BGRA
    }

    const int maskStride = ((size + 31) / 32) * 4;
    for (int y = size - 1; y >= 0; --y) {
        QByteArray mask(maskStride, '\0');
        for (int x = 0; x < size; ++x) {
            if (qAlpha(image.pixel(x, y)) == 0)
                mask[x / 8] = static_cast<char>(mask[x / 8] | (0x80 >> (x % 8)));
        }
        out.writeRawData(mask.constData(), maskStride);
    }
    return bytes;
}

QByteArray icoBytes(const QString& iconsDir) {
    std::vector<std::pair<int, QByteArray>> entries;
    for (const int size : kIconSizes) {
        const QImage image = render(iconsDir, size);
        entries.emplace_back(size, size == kPngEntrySize ? pngBytes(image) : dibBytes(image));
    }

    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out << quint16(0) << quint16(1) << quint16(entries.size()); // ICONDIR, type 1 = icon

    constexpr qsizetype kHeaderSize = 6;
    constexpr qsizetype kEntrySize = 16;
    auto offset = kHeaderSize + kEntrySize * static_cast<qsizetype>(entries.size());
    for (const auto& [size, data] : entries) {
        // ICONDIRENTRY. The size is a byte, so 0 means 256.
        const auto dimension = quint8(size == kPngEntrySize ? 0 : size);
        out << dimension << dimension << quint8(0) << quint8(0) << quint16(1) << quint16(32)
            << quint32(data.size()) << quint32(offset);
        offset += data.size();
    }
    for (const auto& [size, data] : entries)
        out.writeRawData(data.constData(), static_cast<int>(data.size()));
    return bytes;
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        fail(QStringLiteral("could not write %1: %2").arg(path, file.errorString()));
    std::printf("%s (%lld bytes)\n", qUtf8Printable(path), static_cast<long long>(bytes.size()));
}

void generate(const QString& iconsDir, const QString& windowsDir) {
    writeFile(QDir(windowsDir).filePath(QStringLiteral("app.ico")), icoBytes(iconsDir));

    const QDir wizardDir(QDir(windowsDir).filePath(QStringLiteral("wizard-small")));
    if (!wizardDir.mkpath(QStringLiteral(".")))
        fail(QStringLiteral("could not create %1").arg(wizardDir.path()));
    for (const int size : kWizardSizes)
        writeFile(wizardDir.filePath(QStringLiteral("app-%1.png").arg(size)),
                  pngBytes(render(iconsDir, size)));

    writeFile(QDir(iconsDir).filePath(QStringLiteral("app-%1.png").arg(kReadmeSize)),
              pngBytes(render(iconsDir, kReadmeSize)));
}

} // namespace

int main(int argc, char* argv[]) {
    // No QGuiApplication: painting on a QImage needs no platform plugin, so the tool
    // cannot open a window. SVG text would need one, and the icon has none.
    const QCoreApplication app(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() != 3) {
        std::fprintf(stderr, "usage: pmm-icon-gen <icons dir> <windows packaging dir>\n");
        return 2;
    }
    try {
        generate(arguments[1], arguments[2]);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "pmm-icon-gen: %s\n", error.what());
        return 1;
    }
    return 0;
}
