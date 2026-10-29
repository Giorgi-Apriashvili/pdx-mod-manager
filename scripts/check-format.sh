#!/usr/bin/env bash
# Checks that every tracked C/C++ file under src/, tests/ and tools/ matches
# .clang-format, without modifying anything. Exits non-zero and prints the
# offending files when a file would be reformatted.
#
#   scripts/check-format.sh                      # clang-format from PATH
#   CLANG_FORMAT=/path/to/clang-format scripts/check-format.sh
#
# CI pins the clang-format release in .github/workflows/ci.yml; other major
# versions may format differently, so the script warns when they differ.
set -euo pipefail

pinned_major=22
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

clang_format="${CLANG_FORMAT:-clang-format}"
if ! command -v "$clang_format" >/dev/null 2>&1; then
  echo "check-format: '$clang_format' not found; install clang-format $pinned_major.x or set CLANG_FORMAT" >&2
  exit 2
fi

version="$("$clang_format" --version)"
major="$(printf '%s\n' "$version" | sed -n 's/.*version \([0-9][0-9]*\).*/\1/p' | head -n 1)"
if [ "$major" != "$pinned_major" ]; then
  echo "check-format: warning: using clang-format $major, CI uses $pinned_major ($version)" >&2
fi

files="$(git ls-files -- 'src/*.cpp' 'src/*.hpp' 'src/*.h' 'tests/*.cpp' 'tests/*.hpp' \
  'tools/*.cpp' 'tools/*.hpp' ':!:tests/fixtures/*')"
if [ -z "$files" ]; then
  echo "check-format: no files to check" >&2
  exit 0
fi

failed=()
while IFS= read -r file; do
  if ! "$clang_format" --dry-run --Werror --style=file "$file" >/dev/null 2>&1; then
    failed+=("$file")
  fi
done <<< "$files"

total="$(printf '%s\n' "$files" | wc -l | tr -d ' ')"
if [ "${#failed[@]}" -ne 0 ]; then
  printf '%s\n' "${failed[@]}"
  echo "check-format: ${#failed[@]} of $total files need formatting; run scripts/format.sh" >&2
  exit 1
fi
echo "check-format: $total files OK"
