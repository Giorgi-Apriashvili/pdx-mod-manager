#!/usr/bin/env bash
# Reformats every tracked C/C++ file under src/, tests/ and tools/ in place
# with .clang-format. Use the clang-format major version CI pins (see
# scripts/check-format.sh); review the diff before committing.
#
#   scripts/format.sh
#   CLANG_FORMAT=/path/to/clang-format scripts/format.sh
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

clang_format="${CLANG_FORMAT:-clang-format}"
if ! command -v "$clang_format" >/dev/null 2>&1; then
  echo "format: '$clang_format' not found; install clang-format or set CLANG_FORMAT" >&2
  exit 2
fi
"$clang_format" --version

git ls-files -z -- 'src/*.cpp' 'src/*.hpp' 'src/*.h' 'tests/*.cpp' 'tests/*.hpp' \
  'tools/*.cpp' 'tools/*.hpp' ':!:tests/fixtures/*' |
  xargs -0 "$clang_format" -i --style=file
echo "format: done; review with 'git diff'"
