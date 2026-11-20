#pragma once

// Comparison of a descriptor's supported_version pattern ("v4.5.*", "4.4.*",
// "v1.37.*.*", "1.20.*", "*") with an installed game version ("v4.5.2",
// "1.37.5.0", "1.20.1.2").
//
// Pattern: optional leading 'v', then dot-separated components that are
// decimal numbers or '*'. A '*' matches any value; components missing from
// the pattern are unconstrained ("1.37" matches 1.37.5.0); components missing
// from the game version count as 0.
//
// Stellaris additionally rejects wildcards in the major or minor position:
// its logs/error.log reports "Invalid supported_version" for "v4.*.*", "v4.*"
// and "*", but not for "v4.5.*", "4.0.*" or "v4.1.7" (observed on the
// development machine, see docs/games/stellaris.md). Crusader Kings III does
// the same: the error.log of a 1.19 build reports "1.*", and not "1.19.*",
// "1.16.*" or "1.19.0.6" (see docs/games/ck3.md; its rule for "*" and "1.*.*"
// is inferred from Stellaris, and that a two-part pattern without a wildcard
// such as "1.20" is valid is assumed: no descriptor there had one, so the
// error.log cannot tell "a wildcard before the patch" from "fewer than three
// parts". The probe run on 1.20.1.2 could not settle it either: started
// directly, the game reported nothing about descriptors outside enabled_mods,
// so the disabled "1.20" probe was never read; see
// docs/evidence/2026-11-13-ck3-load-order.md). Hearts of Iron IV too: the
// error.log of a 1.19.5.0 run reports each "1.*.*" registration, and not
// "1.19.*", "1.17.*", "1.19.4.*" or the four-part forms without a wildcard.
// Its probe run on 1.20.0.0 (docs/evidence/2026-11-21-hoi4-load-order.md)
// confirmed that: started directly, the game reads every mod/*.mod, enabled
// or not, reported the same "1.*.*" registrations, and did not report the
// disabled probe's "1.20", so a two-part pattern without a wildcard is valid
// there. "*", "1.*" and "1.20.0" are untested for it.
// No such rule is known for EU4: its error.log files on that machine contain
// no such messages, although v1.37.*.* and v1.37.* descriptors are
// registered. Victoria 3 names the key supported_game_version (in
// .metadata/metadata.json) and logs whether it matches. Its probe run on
// 1.15.2 (docs/evidence/2026-11-21-vic3-load-order.md) found no early-wildcard
// rule: "1.*", "1.15" and "1.15.*" were logged as matching 1.15.2, and no
// pattern as invalid. But a lone "*" was logged as not matching 1.15.2, and
// the game listed that mod as out of date (it still loaded it);
// GameTraits::loneWildcardMismatches warns about that, from
// SupportedVersionCheck::loneWildcard.

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
    bool loneWildcard = false;        // the whole pattern is "*" ("v*")
};

// "v4.5.2" -> {4, 5, 2}. Also finds the version inside a label such as
// "Cygnus v4.5.2 (9776)". nullopt when there is no dotted number.
std::optional<std::vector<std::uint32_t>> parseGameVersion(std::string_view text);

// Components of a pattern ("*" for wildcards); nullopt when unparsable.
std::optional<std::vector<std::string>> parseVersionPattern(std::string_view pattern);

SupportedVersionCheck checkSupportedVersion(std::string_view pattern, std::string_view gameVersion);

} // namespace pmm::games
