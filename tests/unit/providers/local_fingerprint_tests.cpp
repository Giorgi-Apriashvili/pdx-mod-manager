#include "providers/local_fingerprint.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <utility>
#include <vector>

using namespace pmm;
using namespace pmm::providers;
using pmm::test::TempDir;
using pmm::test::u8path;
using pmm::test::writeFile;
namespace fs = std::filesystem;

namespace {

// Same tree as the reference implementation that produced kGolden.
std::vector<std::pair<std::string, std::string>> referenceFiles() {
    std::string binary;
    for (int r = 0; r < 300; ++r)
        for (int b = 0; b < 256; ++b)
            binary.push_back(static_cast<char>(b));
    return {
        {"descriptor.mod", "name=\"Example\"\nversion=\"1.0\"\n"},
        {"common/a.txt", "alpha"},
        {"common/B.txt", "beta"},
        {"\xC3\x9Cn\xC3\xAF" "c\xC3\xB8" "d\xC3\xA9 dir/\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.txt",
         "unicode \xE2\x9C\x93"},
        {"empty.txt", ""},
        {"gfx/deep/nested/file.dds", binary},
    };
}

constexpr const char* kGolden =
    "sha256:68f02ff607053aab511d9a2d38db171bcd0ee79321f50c2374531f1a009f0e8d";

void writeTree(const fs::path& root, const std::vector<std::pair<std::string, std::string>>& files) {
    for (const auto& [rel, contents] : files)
        writeFile(root / u8path(rel), contents);
}

} // namespace

TEST_CASE("Fingerprint matches the documented definition", "[providers][fingerprint]") {
    TempDir dir("fp-golden");
    const fs::path root = dir.path() / u8path("Ünïcødé mods") / u8path("日本語 mod");
    writeTree(root, referenceFiles());
    auto fp = fingerprintDirectory(root);
    REQUIRE(fp.ok());
    CHECK(*fp == kGolden);
}

TEST_CASE("Fingerprint is deterministic and independent of creation order",
          "[providers][fingerprint]") {
    TempDir dir("fp-order");
    auto files = referenceFiles();
    writeTree(dir.path() / "forward", files);
    std::vector<std::pair<std::string, std::string>> reversed(files.rbegin(), files.rend());
    writeTree(dir.path() / "reverse", reversed);

    auto a = fingerprintDirectory(dir.path() / "forward");
    auto b = fingerprintDirectory(dir.path() / "reverse");
    auto again = fingerprintDirectory(dir.path() / "forward");
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(again.ok());
    CHECK(*a == *b);
    CHECK(*a == *again);
    CHECK(*a == kGolden);

    // A trailing separator on the root does not change the result.
    auto trailing = fingerprintDirectory(fs::path(dir.path() / "forward") / "");
    REQUIRE(trailing.ok());
    CHECK(*trailing == kGolden);
}

TEST_CASE("Fingerprint skips OS and VCS junk", "[providers][fingerprint]") {
    TempDir dir("fp-junk");
    const fs::path root = dir.path() / "mod";
    writeTree(root, referenceFiles());
    writeFile(root / ".DS_Store", "finder");
    writeFile(root / "common" / "Thumbs.db", "thumbs");
    writeFile(root / "common" / "THUMBS.DB", "thumbs upper");
    writeFile(root / "gfx" / "desktop.ini", "[.ShellClassInfo]");
    writeFile(root / ".git" / "config", "[core]");
    writeFile(root / ".git" / "objects" / "ab" / "cdef", "blob");
    writeFile(root / "__MACOSX" / "common" / "._a.txt", "resource fork");
    writeFile(root / "common" / "._a.txt", "apple double");
    fs::create_directories(root / "empty dir" / "nested");

    auto fp = fingerprintDirectory(root);
    REQUIRE(fp.ok());
    CHECK(*fp == kGolden);

    CHECK(isFingerprintJunk(".DS_Store"));
    CHECK(isFingerprintJunk("a/b/.ds_store"));
    CHECK(isFingerprintJunk("Desktop.INI"));
    CHECK(isFingerprintJunk(".git"));
    CHECK(isFingerprintJunk(".git/HEAD"));
    CHECK(isFingerprintJunk("sub/.git/config"));
    CHECK(isFingerprintJunk("__MACOSX/x"));
    CHECK(isFingerprintJunk("gfx/._icon.dds"));
    CHECK_FALSE(isFingerprintJunk("descriptor.mod"));
    CHECK_FALSE(isFingerprintJunk(".gitignore"));
    CHECK_FALSE(isFingerprintJunk("common/.git_notes.txt"));
    CHECK_FALSE(isFingerprintJunk("my.DS_Store.txt"));
    CHECK_FALSE(isFingerprintJunk("thumbs.db.bak"));
}

TEST_CASE("Fingerprint changes with content, size and path", "[providers][fingerprint]") {
    TempDir dir("fp-change");
    const auto files = referenceFiles();

    auto fingerprintWith = [&](const char* name, auto mutate) {
        auto copy = files;
        mutate(copy);
        const fs::path root = dir.path() / name;
        writeTree(root, copy);
        auto fp = fingerprintDirectory(root);
        REQUIRE(fp.ok());
        return *fp;
    };

    const auto changedByte = fingerprintWith("byte", [](auto& f) { f[1].second = "alphA"; });
    const auto renamed = fingerprintWith("rename", [](auto& f) { f[1].first = "common/c.txt"; });
    const auto moved = fingerprintWith("move", [](auto& f) { f[1].first = "a.txt"; });
    const auto added = fingerprintWith("add", [](auto& f) { f.emplace_back("extra.txt", ""); });
    const auto caseChanged =
        fingerprintWith("case", [](auto& f) { f[2].first = "common/b2.txt"; });

    for (const auto& fp : {changedByte, renamed, moved, added, caseChanged}) {
        CHECK(fp.starts_with("sha256:"));
        CHECK(fp.size() == 7 + 64);
        CHECK(fp != kGolden);
    }
    CHECK(changedByte != renamed);
    CHECK(renamed != moved);
}

TEST_CASE("Fingerprint of an empty folder is the hash of nothing", "[providers][fingerprint]") {
    TempDir dir("fp-empty");
    fs::create_directories(dir.path() / "empty" / "sub");
    writeFile(dir.path() / "empty" / "sub" / "Thumbs.db", "junk only");
    auto fp = fingerprintDirectory(dir.path() / "empty");
    REQUIRE(fp.ok());
    CHECK(*fp == "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("Fingerprint reports missing roots and files instead of folders",
          "[providers][fingerprint]") {
    TempDir dir("fp-missing");
    auto missing = fingerprintDirectory(dir.path() / "nope");
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::NotFound);

    writeFile(dir.path() / "file.zip", "zip");
    auto file = fingerprintDirectory(dir.path() / "file.zip");
    REQUIRE_FALSE(file.ok());
    CHECK(file.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("Fingerprint honours byte/file limits and cancellation", "[providers][fingerprint]") {
    TempDir dir("fp-limits");
    const fs::path root = dir.path() / "mod";
    writeTree(root, referenceFiles());

    FingerprintOptions tight;
    tight.maxTotalBytes = 1000; // the .dds file alone is 76800 bytes
    auto big = fingerprintDirectory(root, tight);
    REQUIRE_FALSE(big.ok());
    CHECK(big.error().code == ErrorCode::LimitExceeded);

    FingerprintOptions fewFiles;
    fewFiles.maxFiles = 3;
    auto many = fingerprintDirectory(root, fewFiles);
    REQUIRE_FALSE(many.ok());
    CHECK(many.error().code == ErrorCode::LimitExceeded);

    std::atomic<bool> cancel{true};
    FingerprintOptions cancelled;
    cancelled.cancel = &cancel;
    auto stopped = fingerprintDirectory(root, cancelled);
    REQUIRE_FALSE(stopped.ok());
    CHECK(stopped.error().code == ErrorCode::Cancelled);

    cancel = false;
    auto resumed = fingerprintDirectory(root, cancelled);
    REQUIRE(resumed.ok());
    CHECK(*resumed == kGolden);
}
