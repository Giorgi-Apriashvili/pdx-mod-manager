#!/usr/bin/env bash
# Runs clang-tidy with the repository's .clang-tidy over tracked C++ sources,
# using the compilation database of a configured build tree. Any finding makes
# the script fail.
#
#   scripts/run-clang-tidy.sh                               # build/core-debug, src/ and tools/
#   scripts/run-clang-tidy.sh build/debug src/sharing/codec.cpp
#   CLANG_TIDY=/path/to/clang-tidy scripts/run-clang-tidy.sh
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

build_dir="${1:-build/core-debug}"
[ "$#" -gt 0 ] && shift
if [ ! -f "$build_dir/compile_commands.json" ]; then
  echo "run-clang-tidy: $build_dir/compile_commands.json not found; configure first (cmake --preset core-debug)" >&2
  exit 2
fi

clang_tidy="${CLANG_TIDY:-clang-tidy}"
if ! command -v "$clang_tidy" >/dev/null 2>&1; then
  echo "run-clang-tidy: '$clang_tidy' not found; install clang-tidy or set CLANG_TIDY" >&2
  exit 2
fi

files=()
if [ "$#" -gt 0 ]; then
  files=("$@")
else
  while IFS= read -r file; do
    files+=("$file")
  done < <(git ls-files -- 'src/*.cpp' 'tools/*.cpp')
fi

failed=0
for file in "${files[@]}"; do
  echo "== $file"
  if ! "$clang_tidy" -p "$build_dir" --quiet --warnings-as-errors='*' "$file"; then
    failed=$((failed + 1))
  fi
done

if [ "$failed" -ne 0 ]; then
  echo "run-clang-tidy: findings in $failed of ${#files[@]} files" >&2
  exit 1
fi
echo "run-clang-tidy: ${#files[@]} files clean"
