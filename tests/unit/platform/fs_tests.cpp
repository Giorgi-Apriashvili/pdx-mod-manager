#include "platform/fs.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

using namespace pmm;
using namespace pmm::platform;
using pmm::test::TempDir;

namespace {

// Makes writing `file` fail with PermissionDenied: read-only attribute on the
// file.
void makeUnwritable(const fs::path& file) {
    fs::permissions(file, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                    fs::perm_options::remove);
}

void makeWritable(const fs::path& file) {
    fs::permissions(file.parent_path(), fs::perms::owner_all, fs::perm_options::add);
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::add);
}

} // namespace

TEST_CASE("writeFileAtomic round-trips binary contents through readFile", "[platform][fs]") {
    TempDir temp;
    const fs::path file = temp.path() / test::unicodeSubdir() / "data file.bin";
    constexpr char kContents[] = "line1\r\nline2\n\0binary\xff\xfe end";
    const std::string contents(kContents, sizeof kContents - 1); // keeps the embedded NUL

    REQUIRE(writeFileAtomic(file, contents).ok()); // creates the missing parents
    const auto read = readFile(file);
    REQUIRE(read.ok());
    CHECK(*read == contents);

    REQUIRE(writeFileAtomic(file, "replaced").ok());
    CHECK(test::readText(file) == "replaced");
    REQUIRE(writeFileAtomic(file, "").ok());
    CHECK(test::readText(file).empty());

    // Only the target remains: no temporary files are left behind.
    CHECK(test::entryNames(file.parent_path()) == std::vector<std::string>{"data file.bin"});
}

TEST_CASE("readFile enforces the size limit and reports missing files", "[platform][fs]") {
    TempDir temp;
    const fs::path file = temp / "big.txt";
    test::writeText(file, std::string(1000, 'x'));

    CHECK(readFile(file, 1000).ok());
    const auto limited = readFile(file, 999);
    REQUIRE_FALSE(limited.ok());
    CHECK(limited.error().code == ErrorCode::LimitExceeded);

    const auto missing = readFile(temp / "missing.txt");
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK(missing.error().message.find("missing.txt") != std::string::npos);
    CHECK_FALSE(missing.error().detail.empty());

    const auto folder = readFile(temp.path());
    REQUIRE_FALSE(folder.ok());
    CHECK(folder.error().code == ErrorCode::IoError);
}

TEST_CASE("writeFileAtomic refuses to replace a folder", "[platform][fs]") {
    TempDir temp;
    const fs::path folder = temp / "settings.txt";
    fs::create_directories(folder / "inner");

    const Status status = writeFileAtomic(folder, "x");
    REQUIRE_FALSE(status.ok());
    CHECK(status.error().code == ErrorCode::IoError);
    CHECK(fs::is_directory(folder / "inner"));
    CHECK(test::entryNames(temp.path()) == std::vector<std::string>{"settings.txt"});
}

TEST_CASE("writeFileAtomic reports PermissionDenied and keeps the original", "[platform][fs]") {
    TempDir temp;
    const fs::path file = temp.path() / test::unicodeSubdir() / "locked.txt";
    test::writeText(file, "original");
    makeUnwritable(file);

    const Status status = writeFileAtomic(file, "new");
    makeWritable(file);
    REQUIRE_FALSE(status.ok());
    CHECK(status.error().code == ErrorCode::PermissionDenied);
    CHECK_FALSE(status.error().detail.empty());
    CHECK(test::readText(file) == "original");
    CHECK(test::entryNames(file.parent_path()) == std::vector<std::string>{"locked.txt"});
}

TEST_CASE("copyFileDurable copies and overwrites", "[platform][fs]") {
    TempDir temp;
    const fs::path source = temp / "source file.txt";
    const fs::path destination = temp.path() / test::unicodeSubdir() / "copy.txt";
    const std::string payload(3 * 1024 * 1024 + 17, 'p'); // spans several copy chunks
    test::writeText(source, payload);
    test::writeText(destination, "old");

    REQUIRE(copyFileDurable(source, destination).ok());
    CHECK(test::readText(destination) == payload);
    CHECK(test::readText(source) == payload);
    CHECK(test::entryNames(destination.parent_path()) == std::vector<std::string>{"copy.txt"});

    const Status missing = copyFileDurable(temp / "nope.txt", destination);
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK(test::readText(destination) == payload);
}

TEST_CASE("isDirectoryWritable probes without leaving files", "[platform][fs]") {
    TempDir temp;
    const fs::path dir = temp.path() / test::unicodeSubdir();
    fs::create_directories(dir);

    const auto writable = isDirectoryWritable(dir);
    REQUIRE(writable.ok());
    CHECK(*writable);
    CHECK(test::entryNames(dir).empty());

    const auto missing = isDirectoryWritable(temp / "missing");
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::NotFound);

    test::writeText(temp / "file.txt", "x");
    const auto notFolder = isDirectoryWritable(temp / "file.txt");
    REQUIRE_FALSE(notFolder.ok());
    CHECK(notFolder.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("UTF-8 path conversion round-trips non-ASCII names", "[platform][fs]") {
    const std::string utf8 = "Ünïcødé dir/日本語 folder/emoji \xF0\x9F\x98\x80.txt";
    const fs::path path = pathFromUtf8(utf8);
    CHECK(toGenericUtf8(path) == utf8);
    CHECK(pathFromUtf8(pathToUtf8(path)) == path);
    CHECK(path.filename() == pathFromUtf8("emoji \xF0\x9F\x98\x80.txt"));
    CHECK(path.native().find(L"\u65E5\u672C\u8A9E") != std::wstring::npos);
    CHECK(toGenericUtf8(fs::path(L"C:\\Users\\Example\\Documents")) ==
          "C:/Users/Example/Documents");
    CHECK(pathToUtf8(fs::path()).empty());
    CHECK(pathFromUtf8("").empty());
}

TEST_CASE("isUtf8Representable rejects paths that have no exact UTF-8 form", "[platform][fs]") {
    CHECK(
        isUtf8Representable(pathFromUtf8("Ünïcødé dir/日本語 folder/emoji \xF0\x9F\x98\x80.txt")));
    CHECK(isUtf8Representable(fs::path()));
    CHECK(isUtf8Representable(fs::path(L"pair \xD83D\xDE00")));
    CHECK_FALSE(isUtf8Representable(fs::path(L"C:\\caf\xD800\\dlc_load.json"))); // lone high
    CHECK_FALSE(isUtf8Representable(fs::path(L"lone \xDC00 low")));
    CHECK_FALSE(isUtf8Representable(fs::path(L"reversed \xDE00\xD83D")));
    CHECK_FALSE(isUtf8Representable(fs::path(L"trailing \xD83D")));
}

TEST_CASE("lastWriteUnixSeconds reflects the modification time", "[platform][fs]") {
    TempDir temp;
    const fs::path file = temp / "stamp.txt";
    test::writeText(file, "x");
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    const std::int64_t stamp = lastWriteUnixSeconds(file);
    CHECK(stamp > now - 300);
    CHECK(stamp < now + 300);
    CHECK(lastWriteUnixSeconds(temp / "missing") == 0);
}

TEST_CASE("isWithin compares normalised paths component by component", "[platform][fs]") {
    TempDir temp;
    const fs::path root = temp.path() / test::unicodeSubdir();
    CHECK(isWithin(root / "a" / "b.txt", root));
    CHECK(isWithin(root, root)); // a path is within itself
    CHECK(isWithin(root / "a", pathFromUtf8(pathToUtf8(root) + "/")));
    CHECK(isWithin(root / "a" / ".." / "b", root));
    CHECK_FALSE(isWithin(root / ".." / "escape.txt", root));
    CHECK_FALSE(isWithin(pathFromUtf8(pathToUtf8(root) + "-sibling/x"), root));
    CHECK_FALSE(isWithin(root.parent_path(), root));
    CHECK_FALSE(isWithin(fs::path(), root));
    CHECK_FALSE(isWithin(root, fs::path()));
    CHECK(isWithin(fs::path(L"C:\\Games\\Steam\\steamapps"), fs::path(L"c:/games/STEAM")));
    CHECK_FALSE(isWithin(fs::path(L"D:\\Games\\Steam"), fs::path(L"C:\\Games")));
}

TEST_CASE("removeFile and createDirectories", "[platform][fs]") {
    TempDir temp;
    const fs::path dir = temp.path() / test::unicodeSubdir() / "deeper";
    REQUIRE(createDirectories(dir).ok());
    REQUIRE(createDirectories(dir).ok()); // already exists
    CHECK(fs::is_directory(dir));

    const fs::path file = dir / "gone.txt";
    test::writeText(file, "x");
    REQUIRE(removeFile(file).ok());
    CHECK_FALSE(fs::exists(file));
    CHECK(removeFile(file).ok()); // missing is fine

    const Status folder = removeFile(dir);
    REQUIRE_FALSE(folder.ok());
    CHECK(folder.error().code == ErrorCode::IoError);
    CHECK(fs::is_directory(dir));

    test::writeText(temp / "blocker", "x");
    const Status blocked = createDirectories(temp / "blocker" / "child");
    REQUIRE_FALSE(blocked.ok());
    CHECK_FALSE(blocked.error().detail.empty());
}
