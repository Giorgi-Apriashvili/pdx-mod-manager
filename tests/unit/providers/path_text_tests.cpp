#include "core/utf8.hpp"
#include "providers/acquisition.hpp"
#include "providers/helper_protocol.hpp"
#include "providers/internal/path_text.hpp"
#include "providers/local_fingerprint.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace pmm;
using namespace pmm::providers;
using pmm::test::TempDir;
using pmm::test::u8path;
using pmm::test::writeFile;
namespace fs = std::filesystem;

// Paths with no UTF-8 form (unpaired UTF-16 surrogates) must be reported, never
// thrown: path::u8string() throws for them.

TEST_CASE("Path text conversion never throws", "[providers][paths]") {
    const fs::path valid = fs::path("mods") / u8path("caf\xC3\xA9") / "a.txt";
    REQUIRE(internal::pathToUtf8(valid, true));
    CHECK(*internal::pathToUtf8(valid, true) == "mods/caf\xC3\xA9/a.txt");
    CHECK(internal::displayPath(u8path("caf\xC3\xA9")) == "caf\xC3\xA9");

    const fs::path lone(L"mods\\caf\xD800");
    CHECK_FALSE(internal::pathToUtf8(lone));
    CHECK_FALSE(internal::pathToUtf8(lone, true));
    CHECK(internal::displayPath(lone) == "mods\\caf\xEF\xBF\xBD");
}

TEST_CASE("A file name with an unpaired surrogate fails fingerprinting cleanly",
          "[providers][paths][fingerprint]") {
    TempDir dir("fp-surrogate");
    const fs::path root = dir.path() / "mod";
    writeFile(root / "descriptor.mod", "name=\"x\"\n");
    writeFile(root / fs::path(L"caf\xD800.txt"), "data");
    REQUIRE(fs::exists(root / fs::path(L"caf\xD800.txt")));

    Result<std::string> fp = fingerprintDirectory(root);
    REQUIRE_FALSE(fp.ok());
    CHECK(fp.error().code == ErrorCode::InvalidArgument);
    CHECK(fp.error().message.find("Rename the file") != std::string::npos);
    CHECK(isValidUtf8(fp.error().detail));
    CHECK(fp.error().detail.find("caf\xEF\xBF\xBD.txt") != std::string::npos);

    // A folder whose own name has the surrogate still fingerprints: only the
    // relative keys must be UTF-8.
    const fs::path oddRoot = dir.path() / fs::path(L"odd\xDC00");
    writeFile(oddRoot / "descriptor.mod", "name=\"x\"\n");
    CHECK(fingerprintDirectory(oddRoot).ok());
}

TEST_CASE("Content checks and helper arguments report paths with unpaired surrogates",
          "[providers][paths]") {
    TempDir dir("verify-surrogate");
    const fs::path folder = dir.path() / fs::path(L"caf\xD800");

    Status gone = verifyWorkshopContent(folder / "703156866");
    REQUIRE_FALSE(gone.ok());
    CHECK(gone.error().code == ErrorCode::NotFound);
    CHECK(isValidUtf8(gone.error().detail));
    CHECK(gone.error().detail.find("caf\xEF\xBF\xBD") != std::string::npos);

    writeFile(folder / "703156866" / "descriptor.mod", "name=\"x\"\n");
    CHECK(verifyWorkshopContent(folder / "703156866").ok());

    Status badName =
        validateHelperArguments("281990", dir.path() / fs::path(L"steam_api64\xD800.dll"));
    REQUIRE_FALSE(badName.ok());
    CHECK(badName.error().code == ErrorCode::InvalidArgument);
    CHECK(isValidUtf8(badName.error().detail));

    Status missing = validateHelperArguments("281990", folder / "steam_api64.dll");
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK(isValidUtf8(missing.error().detail));
}
