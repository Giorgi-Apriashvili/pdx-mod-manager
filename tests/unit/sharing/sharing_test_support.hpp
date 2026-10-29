#pragma once

// Helpers for crafting share codes in tests: a correct envelope around
// arbitrary compressed bytes or JSON, so each test reaches exactly the decoding
// stage it targets.

#include "core/sha256.hpp"
#include "sharing/codec.hpp"
#include "sharing/manifest.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace pmm::sharing::test {

inline std::string codeFromCompressed(std::string_view compressed) {
    return "PMM1." + base64UrlEncode(compressed) + "." + sha256Hex(compressed);
}

inline std::string codeFromJson(std::string_view json) {
    auto compressed = zlibCompress(json);
    REQUIRE(compressed.ok());
    return codeFromCompressed(*compressed);
}

inline std::string readFixture(std::string_view relative) {
    const std::filesystem::path path =
        std::filesystem::path(PMM_FIXTURES_DIR) / "sharing" / std::filesystem::path(relative);
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline Manifest sampleManifest() {
    Manifest m;
    m.game = "stellaris";
    m.playsetName = "Sample playset";
    m.gameVersionHint = "v4.1.5";
    m.mods = {
        {"steam-workshop", "1234567890", "First mod", true, "", ""},
        {"steam-workshop", "1234567891", "Disabled mod", false, "2.0", ""},
        {"local", "my-local-mod", "Local mod", true, "", "sha256:" + std::string(64, 'a')},
        {"steam-workshop", "18446744073709551615", "Ünïcødé 日本語 🚀", false, "", ""},
    };
    return m;
}

// Minimal valid manifest JSON with `mods` and extra root members spliced in.
inline std::string manifestJson(std::string_view mods = "[]", std::string_view extraRoot = "") {
    std::string json = R"({"schemaVersion":1,"game":"stellaris","playsetName":"P",)";
    json += extraRoot;
    json += R"("mods":)";
    json += mods;
    json += "}";
    return json;
}

} // namespace pmm::sharing::test
