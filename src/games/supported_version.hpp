#pragma once

// Comparison of a descriptor's supported_version pattern ("v4.5.*", "4.4.*",
// "v1.37.*.*", "*") with an installed game version ("v4.5.2", "1.37.5.0").
//
// Pattern: optional leading 'v', then dot-separated components that are
// decimal numbers or '*'. A '*' matches any value; components missing from
// the pattern are unconstrained ("1.37" matches 1.37.5.0); components missing
// from the game version count as 0.
//
// Stellaris additionally rejects wildcards in the major or minor position:
// its logs/error.log reports "Invalid supported_version" for "v4.*.*", "v4.*"
// and "*", but not for "v4.5.*", "4.0.*" or "v4.1.7" (observed on the
// development machine, see docs/games/stellaris.md). No such rule is known for
// EU4: its leftover error.log on that machine contains no such messages.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::games {

enum class VersionMatch {
    NotDeclared, // empty pattern
    Matches,
    Mismatch,
    Unparsable,         // not a version pattern at all
    UnknownGameVersion, // the installation's version could not be read
};

struct SupportedVersionCheck {
    VersionMatch match = VersionMatch::NotDeclared;
    bool wildcardBeforePatch = false; // '*' as major or minor component
};

// "v4.5.2" -> {4, 5, 2}. Also finds the version inside a label such as
// "Cygnus v4.5.2 (9776)". nullopt when there is no dotted number.
std::optional<std::vector<std::uint32_t>> parseGameVersion(std::string_view text);

// Components of a pattern ("*" for wildcards); nullopt when unparsable.
std::optional<std::vector<std::string>> parseVersionPattern(std::string_view pattern);

SupportedVersionCheck checkSupportedVersion(std::string_view pattern, std::string_view gameVersion);

} // namespace pmm::games
