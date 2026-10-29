#include "games/supported_version.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>
#include <vector>

using namespace pmm::games;

TEST_CASE("Game versions are read from raw versions and labels", "[games][version]") {
    CHECK(parseGameVersion("v4.5.2") == std::vector<std::uint32_t>{4, 5, 2});
    CHECK(parseGameVersion("Cygnus v4.5.2 (9776)") == std::vector<std::uint32_t>{4, 5, 2});
    CHECK(parseGameVersion("1.37.5.0") == std::vector<std::uint32_t>{1, 37, 5, 0});
    CHECK(parseGameVersion("EU4 v1.37.5.0 Inca") == std::vector<std::uint32_t>{1, 37, 5, 0});
    CHECK_FALSE(parseGameVersion("").has_value());
    CHECK_FALSE(parseGameVersion("v4").has_value());
    CHECK_FALSE(parseGameVersion("build (9776)").has_value());
}

TEST_CASE("Version patterns are split into components", "[games][version]") {
    CHECK(parseVersionPattern("v4.5.*") == std::vector<std::string>{"4", "5", "*"});
    CHECK(parseVersionPattern(" 4.4.* ") == std::vector<std::string>{"4", "4", "*"});
    CHECK(parseVersionPattern("*") == std::vector<std::string>{"*"});
    CHECK_FALSE(parseVersionPattern("").has_value());
    CHECK_FALSE(parseVersionPattern("v").has_value());
    CHECK_FALSE(parseVersionPattern("latest").has_value());
    CHECK_FALSE(parseVersionPattern("4..5").has_value());
    CHECK_FALSE(parseVersionPattern("4.5.").has_value());
    CHECK_FALSE(parseVersionPattern("4.x").has_value());
}

TEST_CASE("supported_version patterns are compared with the installed version",
          "[games][version]") {
    struct Case {
        std::string pattern;
        std::string game;
        VersionMatch match;
        bool early;
    };
    // Stellaris patterns observed on the development machine against v4.5.2,
    // plus EU4 patterns against v1.37.5.0.
    const auto c = GENERATE(Case{"v4.5.*", "v4.5.2", VersionMatch::Matches, false},
                            Case{"v4.4.*", "v4.5.2", VersionMatch::Mismatch, false},
                            Case{"4.0.*", "v4.5.2", VersionMatch::Mismatch, false},
                            Case{"3.10.*", "v4.5.2", VersionMatch::Mismatch, false},
                            Case{"v4.5.2", "v4.5.2", VersionMatch::Matches, false},
                            Case{"v4.1.7", "v4.5.2", VersionMatch::Mismatch, false},
                            Case{"v4.*.*", "v4.5.2", VersionMatch::Matches, true},
                            Case{"v4.*", "v4.5.2", VersionMatch::Matches, true},
                            Case{"*", "v4.5.2", VersionMatch::Matches, true},
                            Case{"v3.*", "v4.5.2", VersionMatch::Mismatch, true},
                            Case{"latest", "v4.5.2", VersionMatch::Unparsable, false},
                            Case{"", "v4.5.2", VersionMatch::NotDeclared, false},
                            Case{"v4.5.*", "", VersionMatch::UnknownGameVersion, false},
                            Case{"v4.5.*", "Cygnus v4.5.2 (9776)", VersionMatch::Matches, false},
                            Case{"v1.37.*.*", "v1.37.5.0", VersionMatch::Matches, false},
                            Case{"1.*.*.*", "v1.37.5.0", VersionMatch::Matches, true},
                            Case{"v1.37.*", "v1.37.5.0", VersionMatch::Matches, false},
                            Case{"v1.37.5.0", "v1.37.5.0", VersionMatch::Matches, false},
                            Case{"1.34.5", "v1.37.5.0", VersionMatch::Mismatch, false},
                            Case{"1.35.*", "1.37.5.0", VersionMatch::Mismatch, false},
                            Case{"4.5.2.0", "v4.5.2", VersionMatch::Matches, false},
                            Case{"4.5.2.1", "v4.5.2", VersionMatch::Mismatch, false});
    CAPTURE(c.pattern, c.game);
    const SupportedVersionCheck check = checkSupportedVersion(c.pattern, c.game);
    CHECK(check.match == c.match);
    CHECK(check.wildcardBeforePatch == c.early);
}
