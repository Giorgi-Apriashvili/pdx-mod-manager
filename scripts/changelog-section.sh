#!/usr/bin/env bash
# Prints the body of the CHANGELOG.md section for a version, for the release
# notes. Accepts the headings "## [1.2.3] - 2026-11-01", "## [1.2.3]" and
# "## 1.2.3". Exits 1 when the section is missing or empty.
#
#   scripts/changelog-section.sh 1.2.3 [CHANGELOG.md]
set -euo pipefail

version="${1:?usage: changelog-section.sh <version> [changelog file]}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
changelog="${2:-$root/CHANGELOG.md}"

# Link reference definitions ("[1.2.3]: https://...") are left out, and blank
# lines around the body are trimmed.
body="$(tr -d '\r' < "$changelog" | awk -v version="$version" '
  /^## / {
    if (found) exit
    heading = $2
    gsub(/[][]/, "", heading)
    if (heading == version) { found = 1; next }
  }
  found && !/^\[[^]]+\]: / {
    if ($0 == "") { blanks++; next }
    if (printed) { for (i = 0; i < blanks; i++) print "" }
    blanks = 0
    printed = 1
    print
  }
')"

if [ -z "$body" ]; then
  echo "changelog-section: no entries for $version in $changelog" >&2
  exit 1
fi
printf '%s\n' "$body"
