#!/usr/bin/env bash
# The checks to run before pushing. GitHub runs CI only for a release
# (docs/releasing.md), so these are what keeps main green.
#
#   scripts/check.sh           quick: the debug build with Qt and every test in it
#                              (the QML preview tests too, label ui-preview),
#                              formatting, QML lint, the update contract's sources
#                              and the UI contrast check (a few minutes)
#   scripts/check.sh --full    everything CI runs: also ASan and clang-cl with UBSan
#                              on the Qt-free libraries and the tests of the contract
#                              check; plus clang-tidy on the C++ files changed since
#                              origin/main, and the `ci` preset (warnings as errors)
#                              built and tested from a fresh checkout of HEAD, which
#                              catches files that were never committed
#
# Git Bash on Windows. QT_ROOT_DIR defaults to C:/Qt/6.10.3/msvc2022_64. Stops at
# the first failing step.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

full=false
case "${1:-}" in
  "" | --quick) ;;
  --full) full=true ;;
  *)
    echo "usage: scripts/check.sh [--quick | --full]" >&2
    exit 2
    ;;
esac

export QT_ROOT_DIR="${QT_ROOT_DIR:-C:/Qt/6.10.3/msvc2022_64}"
export QT_QPA_PLATFORM=offscreen
export PATH="$(cygpath -u "$QT_ROOT_DIR")/bin:$PATH"
if ! command -v cl >/dev/null 2>&1; then
  # shellcheck source=scripts/msvc-env.sh
  source scripts/msvc-env.sh
fi
# clang-format, clang-tidy and clang-cl (LLVM 22), after the Visual Studio tools.
llvm="/c/Program Files/LLVM/bin"
[ -d "$llvm" ] && export PATH="$PATH:$llvm"
jobs="$(nproc 2>/dev/null || echo 8)"
started=$SECONDS

step() {
  echo
  echo "== $1"
}

# Configures `preset` into build/<dir> unless that tree exists, then builds it.
build() {
  local preset="$1" dir="$2"
  if [ ! -f "build/$dir/CMakeCache.txt" ]; then
    cmake --preset "$preset" -B "build/$dir"
  fi
  cmake --build "build/$dir"
}

step "debug build (Qt application and every library)"
CC=cl CXX=cl build debug debug
step "tests (debug)"
# The QML preview tests are registered only when Qt's qmltestrunner is found.
if ! ctest --test-dir build/debug -N -L ui-preview | tr -d '\r' | grep -q '^Total Tests: [1-9]'; then
  echo "the QML preview tests (label ui-preview) are not registered: qmltestrunner not found in $QT_ROOT_DIR/bin" >&2
  exit 1
fi
ctest --test-dir build/debug -j "$jobs" --output-on-failure
step "formatting"
bash scripts/check-format.sh
step "QML lint"
bash ui/dev/preview.sh --lint
step "update contract (sources)"
bash scripts/check-update-contract.sh
step "UI contrast"
python ui/dev/check_contrast.py

if $full; then
  step "ASan (core-sanitize)"
  CC=cl CXX=cl build core-sanitize core-sanitize
  ctest --preset core-sanitize -j "$jobs"

  step "clang-cl with UBSan (core-clang-cl)"
  build core-clang-cl core-clang-cl
  ctest --preset core-clang-cl -j "$jobs"

  step "tests of the update contract check"
  bash scripts/test-update-contract.sh

  step "clang-tidy (C++ files changed since origin/main)"
  mapfile -t changed < <(
    { git diff --name-only --diff-filter=d origin/main -- 'src/*.cpp' 'tests/*.cpp' 'tools/*.cpp'
      git ls-files --others --exclude-standard -- 'src/*.cpp' 'tests/*.cpp' 'tools/*.cpp'; } | sort -u)
  if [ "${#changed[@]}" -eq 0 ]; then
    echo "no changed C++ sources"
  else
    bash scripts/run-clang-tidy.sh build/debug "${changed[@]}"
  fi

  step "ci preset from a fresh checkout of HEAD"
  if [ -n "$(git status --porcelain)" ]; then
    echo "note: uncommitted changes are not part of this build"
  fi
  scratch="$(mktemp -d)"
  fresh="$scratch/pmm-check"
  cleanup() {
    git worktree remove --force "$fresh" >/dev/null 2>&1 || true
    rm -rf "$scratch"
  }
  trap cleanup EXIT
  git worktree add --detach --quiet "$fresh" HEAD
  # The dependencies the debug tree has already downloaded, read only.
  deps=()
  for dep in nlohmann_json pmm_zlib_src pmm_sqlite_src catch2; do
    if [ -d "build/debug/_deps/$dep-src" ]; then
      deps+=("-DFETCHCONTENT_SOURCE_DIR_${dep^^}=$root/build/debug/_deps/$dep-src")
    fi
  done
  (cd "$fresh" && CC=cl CXX=cl cmake --preset ci "${deps[@]}" > /dev/null &&
    cmake --build build/ci && ctest --test-dir build/ci -j "$jobs" --output-on-failure)
fi

echo
echo "check: all $($full && echo full || echo quick) checks passed in $(((SECONDS - started) / 60)) min $(((SECONDS - started) % 60)) s"
