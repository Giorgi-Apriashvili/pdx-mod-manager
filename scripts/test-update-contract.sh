#!/usr/bin/env bash
# Tests scripts/check-update-contract.sh (CI, "Format check"): the sources pass,
# and a copy of them with any one contract value changed fails, also when the old
# value is left in a comment, while comment markers in strings pass; versions and
# release assets that a released copy would reject are refused, and the published
# releases are compared through a stub gh. No network.
#
#   scripts/test-update-contract.sh
set -euo pipefail
export LC_ALL=C

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ -n "$root" ] || exit 2
work="$(mktemp -d)"
[ -n "$work" ] && [ -d "$work" ] || exit 2
trap 'rm -rf -- "$work"' EXIT
failures=0
count=0
reason=""

# because <text>: the next expect also needs <text> in the output, so that a
# failure counts only for the reason the case is about.
because() {
  reason="$1"
}

# expect <exit code> <description> <command...>
expect() {
  local want="$1" what="$2" code
  shift 2
  count=$((count + 1))
  if "$@" > "$work/out" 2>&1; then code=0; else code=$?; fi
  if [ "$code" = "$want" ] && { [ -z "$reason" ] || grep -F -q -- "$reason" "$work/out"; }; then
    echo "ok: $what"
  else
    echo "FAILED: $what (exit $code, expected $want${reason:+ and the text: $reason})"
    sed 's/^/    /' "$work/out"
    failures=$((failures + 1))
  fi
  reason=""
}

# ---- sources -------------------------------------------------------------

sources=(
  CMakeLists.txt packaging/update-contract.txt scripts/check-update-contract.sh
  src/services/app_update.hpp src/services/app_update.cpp
  src/app/core/update_client.hpp src/app/core/update_client.cpp
  src/app/core/running_instance.hpp src/app/core/running_instance.cpp
  packaging/windows/installer.iss packaging/windows/smoke-test-installer.ps1
  packaging/windows/prune-qt-deployment.cmake
  .github/workflows/release.yml .github/workflows/ci.yml docs/updates.md
)
copy="$work/copy"

fresh_copy() {
  local file
  rm -rf -- "$copy"
  for file in "${sources[@]}"; do
    mkdir -p "$copy/$(dirname "$file")"
    cp -- "$root/$file" "$copy/$file"
  done
}

# edit <description> <sed script> <file>...: a fresh copy with the change, which
# must apply to at least one of the files; 1, counted as a failure, if it does not.
edit() {
  local what="$1" script="$2" file changed=false
  shift 2
  fresh_copy
  for file in "$@"; do
    sed -i -e "$script" "$copy/$file"
    cmp -s "$root/$file" "$copy/$file" || changed=true
  done
  if [ "$changed" != true ]; then
    count=$((count + 1))
    failures=$((failures + 1))
    echo "FAILED: $what: the change did not apply to $*"
    reason=""
    return 1
  fi
}

# mutate <description> <sed script> <file>...: the check must find the change.
mutate() {
  edit "$@" || return 0
  [ -n "$reason" ] || because 'drift:'
  expect 1 "$1 is found" bash "$copy/scripts/check-update-contract.sh"
}

# harmless <description> <sed script> <file>...: the check must still pass.
harmless() {
  edit "$@" || return 0
  expect 0 "$1 is not a drift" bash "$copy/scripts/check-update-contract.sh"
}

fresh_copy
expect 0 'the sources follow the contract' bash "$copy/scripts/check-update-contract.sh"

update=(src/services/app_update.hpp src/services/app_update.cpp)
mutate 'another repository id in CMake' 's/1439071956/1439071957/' CMakeLists.txt
mutate 'the API asked by name' 's|/repositories/|/repos/|g' "${update[@]}"
mutate 'another mutex in the application' 's/ParadoxModManagerRunning/ParadoxModManagerRunning2/' \
  src/app/core/running_instance.hpp
mutate 'another update argument' 's|"/PMMUPDATE=1"|"/PMMUPDATE=2"|' "${update[@]}"
mutate 'one update argument less' 's|"/SP-", ||' "${update[@]}"
mutate 'another installer name' 's|-windows-x86_64-setup.exe|-windows-x64-setup.exe|' "${update[@]}"
mutate 'another checksum file' 's|"SHA256SUMS.txt"|"SHA256SUMS"|' "${update[@]}"
mutate 'a larger installer limit' 's|512ll \* 1024|1024ll * 1024|' src/services/app_update.hpp
mutate 'a smaller checksum limit' 's|std::size_t{64} \* 1024|std::size_t{32} * 1024|' src/services/app_update.hpp
mutate 'another installation marker' 's|"unins000.exe"|"uninstall.exe"|' "${update[@]}"
mutate 'a token in the request' '/UserAgentHeader/a request.setRawHeader("Authorization", "token x");' \
  src/app/core/update_client.cpp
iss=packaging/windows/installer.iss
mutate 'another AppId' 's/25EA6705-9979/25EA6705-9978/g' "$iss"
mutate 'an installation for all users' 's/^PrivilegesRequired=lowest/PrivilegesRequired=admin/' "$iss"
mutate 'a choice of installation mode' \
  's/^PrivilegesRequired=lowest/&\nPrivilegesRequiredOverridesAllowed=dialog/' "$iss"
mutate 'the uninstaller in another folder' 's/^PrivilegesRequired=lowest/&\nUninstallFilesDir={app}\\uninstall/' "$iss"
mutate 'the application in another folder' 's/^#define AppExe .*/#define AppExe "pmm-app.exe"/' "$iss"
mutate 'another update parameter in the setup' 's/{param:PMMUPDATE|0}/{param:UPDATE|0}/' "$iss"
mutate 'another mutex in the setup' 's/^#define AppRunningMutex .*/#define AppRunningMutex "Local\\Other"/' "$iss"
mutate 'another setup name' 's/^OutputBaseFilename=.*/OutputBaseFilename=pmm-setup/' "$iss"
because 'installer.iss starts the application again after an update run'
mutate 'no relaunch after an update run' 's/; Check: IsPmmUpdate/; Check: not IsPmmUpdate/' "$iss"
smoke=packaging/windows/smoke-test-installer.ps1
mutate 'no TLS plugin in the smoke test' 's/.plugins\\tls\\qschannelbackend\.dll.,//' "$smoke"
mutate 'other update arguments in the smoke test' "s|'/PMMUPDATE=1')|'/PMMUPDATE=1', '/NOICONS')|" "$smoke"
mutate 'no TLS check in packages' 's|/tls/qschannelbackend.dll|/tls/qopensslbackend.dll|' \
  packaging/windows/prune-qt-deployment.cmake
mutate 'no asset check in the workflow' 's/check-update-contract.sh assets/check-update-contract.sh get/' \
  .github/workflows/release.yml
mutate 'no contract check in CI' '/check-update-contract.sh/d' .github/workflows/ci.yml
mutate 'the API address missing from the documentation' \
  's|repositories/1439071956/releases/latest|repositories/<id>/releases/latest|g' docs/updates.md
contract_file=packaging/update-contract.txt
mutate 'a changed contract value' 's/^running_mutex = .*/running_mutex = Local\\Other/' "$contract_file"
because "version_regex accepts '01.0.0'"
mutate 'a version grammar with leading zeros' \
  's/^version_regex = .*/version_regex = ^[0-9]+\\.[0-9]+\\.[0-9]+$/' "$contract_file"

# Comments: a value that is left only in a comment is a drift, in each syntax the
# check reads; comment markers in strings are code.
cpp=src/services/app_update.cpp
because 'the API request asks for releases/latest'
mutate '/releases/latest only in a C++ comment' 's|out += "/releases/latest";|// &|' "$cpp"
because 'the application starts the setup with'
mutate 'the update arguments only in a C++ block comment' 's|return {"/SILENT".*};|return {}; /* & */|' "$cpp"
because 'kMaxInstallerBytes'
mutate 'a larger installer limit with the old one in a comment' \
  's|kMaxInstallerBytes = 512ll \* 1024 \* 1024;|kMaxInstallerBytes = 1024ll * 1024 * 1024; // was &|' \
  src/services/app_update.hpp
harmless 'a C++ string with // and /* before a value' \
  's|out += "/releases/latest";|out += "/*//" "/releases/latest";|' "$cpp"
harmless 'a digit separator, then a string with a quote and //' \
  "s|out += \"/releases/latest\";|n = 1'000; s = \"'//\"; &|" "$cpp"
harmless 'a quote in a char literal, then a string with //' \
  "s|out += \"/releases/latest\";|c = '\"'; s = \"//\"; &|" "$cpp"
harmless 'a quote in a raw string, then a string with /*' \
  "s|out += \"/releases/latest\";|r = R\"x(\")x\"; out += \"/*//\" \"/releases/latest\";|" "$cpp"
harmless 'a token in a commented-out line' \
  '/UserAgentHeader/a // request.setRawHeader("Authorization", "token x");' src/app/core/update_client.cpp
because 'installer.iss checks AppMutex'
mutate 'the AppMutex line commented out' 's/^AppMutex=/; &/' "$iss"
# The preprocessor drops a line that starts with //, in every section.
because 'installer.iss checks AppMutex'
mutate 'the AppMutex line behind //' 's|^AppMutex=|// &|' "$iss"
because 'the installation is per-user'
mutate 'the per-user installation behind an indented //' 's|^PrivilegesRequired=lowest|  // &|' "$iss"
because 'installer.iss waits for Local\ParadoxModManagerRunning'
mutate 'another mutex in the setup, the old one behind //' \
  's|^#define AppRunningMutex .*|// &\n#define AppRunningMutex "Local\\Other"|' "$iss"
because 'installer.iss polls the mutex'
mutate 'the mutex polls in a Pascal comment with a } behind //' \
  's|^  Waited := 0;$|{\n// }\n&|; s|^  Result := not CheckForMutexes.*|&\n}|' "$iss"
because 'installer.iss starts the application again after an update run'
mutate 'the relaunch after an update run commented out' '/; Check: IsPmmUpdate/s/^/; /' "$iss"
because 'installer.iss recognises /PMMUPDATE=1'
mutate 'the update parameter only in a Pascal comment' '/{param:PMMUPDATE|0}/s|Result := |&False; // |' "$iss"
because 'installer.iss polls the mutex'
mutate 'the mutex poll only in Pascal block comments' "s/CheckForMutexes('{#AppRunningMutex}')/False (* & *)/g" "$iss"
harmless 'a Pascal string with a quote, braces and comment markers before a value' \
  "s|Result := ExpandConstant(|Log('it''s { (* // '); &|" "$iss"
because 'installer.iss documents /SILENT /SP- /NORESTART /PMMUPDATE=1'
mutate 'the update arguments missing from the comment at the top of installer.iss' \
  's|with /SILENT /SP- /NORESTART /PMMUPDATE=1 and|with the update arguments and|' "$iss"
because 'the smoke test runs the setup with'
mutate 'the update arguments only in a PowerShell comment' \
  "s/^\(\$updateArguments = \)\(@(.*)\)/\1@() # \2/" "$smoke"
because 'the smoke test holds'
mutate 'the mutex only in a PowerShell block comment' \
  "s/^\(\$mutexName = \)\('[^']*'\)/\1'Local\\\\Other' <# \2 #>/" "$smoke"
harmless 'PowerShell strings with # and quotes before a value' \
  "s/^\$mutexName = /&\"\`\" #\" + 'it''s <# ' + /" "$smoke"
because 'CI runs this check'
mutate 'the contract check of CI only in a YAML comment' \
  's|bash scripts/check-update-contract.sh|true # &|' .github/workflows/ci.yml
because 'the workflow checks the assets'
mutate 'the asset check of the workflow in a commented-out step' \
  '/- name: Check the release against the update contract/,/published "\$VERSION"/s/^/#/' \
  .github/workflows/release.yml
harmless 'quoted # and ${#...} in a workflow before a value' \
  "s|bash scripts/check-update-contract.sh\$|echo \${#GITHUB_SHA} \"\\\\\" #1\" ' #2' \&\& &|" .github/workflows/ci.yml
because 'PMM_GITHUB_REPOSITORY_ID is 1439071956'
mutate 'the repository id only in a CMake comment' \
  's/^set(PMM_GITHUB_REPOSITORY_ID 1439071956)/set(PMM_GITHUB_REPOSITORY_ID 1439071957) # &/' CMakeLists.txt
because 'packages must have'
mutate 'the TLS plugin check only in a CMake bracket comment' \
  's|^set(_pmm_tls .*|set(_pmm_tls "${QT_DEPLOY_PREFIX}") #[==[\n&\n]==]|' \
  packaging/windows/prune-qt-deployment.cmake
harmless 'CMake strings and a bracket argument with # before a value' \
  's|^  HOMEPAGE_URL|  DESCRIPTION "C# \\" #[[" [=[ # ]=]&|' CMakeLists.txt

fresh_copy
printf '%s\n' '// No Authorization header and no token are ever sent.' >> "$copy/src/app/core/update_client.cpp"
expect 0 'a comment about credentials is not a drift' bash "$copy/scripts/check-update-contract.sh"

fresh_copy
sed -i -e '/^app_id = /d' "$copy/packaging/update-contract.txt"
because "no single value for 'app_id'"
expect 2 'a value missing from the contract stops the check' bash "$copy/scripts/check-update-contract.sh"

# An awk that fails on the Inno Setup script, before any of its checks ran.
mkdir -p "$work/failing-awk"
cat > "$work/failing-awk/awk" << EOF
#!/usr/bin/env bash
case "\$*" in *syntax=inno*) echo 'awk: simulated failure' >&2; exit 2 ;; esac
exec '$(command -v awk)' "\$@"
EOF
chmod +x "$work/failing-awk/awk"
fresh_copy
because 'could not read the code of packaging/windows/installer.iss'
expect 2 'a comment stripper that fails stops the check' \
  env PATH="$work/failing-awk:$PATH" bash "$copy/scripts/check-update-contract.sh"

# ---- versions ------------------------------------------------------------

check="$root/scripts/check-update-contract.sh"
for version in 0.1.0 1.0.0 10.2.30 65535.0.0; do
  expect 0 "version $version is allowed" bash "$check" version "$version"
done
for version in 01.0.0 0.01.0 0.1 0.1.0.0 0.1.0-rc1 v0.1.0 65536.0.0 ' 0.1.0' '0.1.0 ' ''; do
  because 'is not <major>.<minor>.<patch>'
  expect 1 "version '$version' is refused" bash "$check" version "$version"
done
expect 0 'get prints a value' bash -c "[ \"\$(bash '$check' get repository_id)\" = 1439071956 ]"
expect 2 'get of an unknown key fails' bash "$check" get no_such_key

# ---- published releases, through a stub gh ---------------------------------

mkdir -p "$work/bin"
cat > "$work/bin/gh" << 'EOF'
#!/usr/bin/env bash
# Answers like gh api with --jq applied: STUB_RELEASES holds "<tag> <prerelease>"
# lines, STUB_LATEST the latest tag (empty: HTTP 404), STUB_LATEST_ERROR another error.
case "$*" in
  *"/releases/latest"*)
    if [ -n "${STUB_LATEST_ERROR:-}" ]; then
      echo "gh: $STUB_LATEST_ERROR" >&2
      exit 1
    fi
    if [ -z "${STUB_LATEST:-}" ]; then
      echo 'gh: Not Found (HTTP 404)' >&2
      exit 1
    fi
    echo "$STUB_LATEST"
    ;;
  *"/releases"*) printf '%b' "${STUB_RELEASES:-}" ;;
  *)
    echo "stub gh: unexpected $*" >&2
    exit 3
    ;;
esac
EOF
chmod +x "$work/bin/gh"

published() { # published <exit code> <description> <releases> <latest> <version> [<error>]
  expect "$1" "$2" env PATH="$work/bin:$PATH" GH_REPO=owner/name STUB_RELEASES="$3" \
    STUB_LATEST="$4" STUB_LATEST_ERROR="${6:-}" bash "$check" published "$5"
}

published 0 'the first release, nothing published' '' '' 0.1.0
published 0 'a higher version than the latest' 'v0.1.0 false\n' v0.1.0 0.1.1
because 'release v0.1.0 already exists'
published 1 'the version of the latest release' 'v0.1.0 false\n' v0.1.0 0.1.0
because 'is not higher than the published release v0.2.0'
published 1 'a lower version than the latest' 'v0.2.0 false\nv0.1.0 false\n' v0.2.0 0.1.9
because 'is not higher than the published release v0.3.0'
published 1 'a version below an older release that is no longer latest' 'v0.3.0 false\nv0.2.0 false\n' v0.2.0 0.2.5
because 'is not higher than the published release v0.2.0'
published 1 'a version below a pre-release' 'v0.2.0 true\n' '' 0.1.0
published 0 'a version above a pre-release' 'v0.2.0 true\n' '' 0.2.1
published 0 'a tag that is not a version is ignored' 'nightly true\n' '' 0.1.0
because 'GitHub reports no latest release'
published 1 'no latest release although one is published' 'v0.1.0 false\n' '' 0.2.0
because "the tag 'v0.1.0-rc1', which is not"
published 1 'a latest release with a tag that is not a version' 'v0.1.0-rc1 false\n' v0.1.0-rc1 0.2.0
because 'could not read the latest release'
published 2 'GitHub failing' '' '' 0.1.0 'Server Error (HTTP 500)'
because 'is not <major>.<minor>.<patch>'
published 1 'a version with a leading zero' '' '' 0.01.0

# ---- assets ----------------------------------------------------------------

version=0.1.0
setup="ParadoxModManager-$version-windows-x86_64-setup.exe"
zip="ParadoxModManager-$version-windows-x86_64.zip"
assets="$work/assets"

# fresh_assets: the files of a release as the workflow makes them.
fresh_assets() {
  rm -rf -- "$assets"
  mkdir -p "$assets"
  printf 'setup' > "$assets/$setup"
  printf 'zip' > "$assets/$zip"
  (cd "$assets" || exit 1; sha256sum --text -- * > ../SHA256SUMS.txt)
  mv -- "$work/SHA256SUMS.txt" "$assets/SHA256SUMS.txt"
}

sums="$assets/SHA256SUMS.txt"
assets_case() { # assets_case <exit code> <description> <shell command run in a fresh copy>
  fresh_assets
  (cd "$assets" || exit 1; eval "$3")
  expect "$1" "$2" bash "$check" assets "$assets" "$version"
}

assets_case 0 'the assets as the workflow makes them' ':'
because 'the installer ParadoxModManager-0.1.0-windows-x86_64-setup.exe is missing'
assets_case 1 'no installer' "rm -- '$setup'; grep -v -- '$setup' SHA256SUMS.txt > s; mv s SHA256SUMS.txt"
because 'SHA256SUMS.txt is missing'
assets_case 1 'no checksum file' 'rm SHA256SUMS.txt'
because 'starts with a BOM'
assets_case 1 'a BOM' "{ printf '\\357\\273\\277'; cat SHA256SUMS.txt; } > s; mv s SHA256SUMS.txt"
because 'has CR characters'
assets_case 1 'CRLF line ends' "sed -i 's/\$/\\r/' SHA256SUMS.txt"
because 'line 3 of SHA256SUMS.txt is not'
assets_case 1 'a blank line' 'echo >> SHA256SUMS.txt'
because 'line 1 of SHA256SUMS.txt is not'
assets_case 1 'a comment line' "sed -i '1i # comment' SHA256SUMS.txt"
because 'does not end with LF'
assets_case 1 'no LF after the last line' "printf '%s' \"\$(cat SHA256SUMS.txt)\" > s; mv s SHA256SUMS.txt"
because 'line 1 of SHA256SUMS.txt is not'
assets_case 1 'an uppercase digest' "sed -i '1s/^\\(.*\\)\\(  .*\\)\$/\\U\\1\\E\\2/' SHA256SUMS.txt"
because 'line 1 of SHA256SUMS.txt is not'
assets_case 1 'a binary-mode line' "sed -i '1s/  / */' SHA256SUMS.txt"
because 'line 1 of SHA256SUMS.txt is not'
assets_case 1 'one space' "sed -i '1s/  / /' SHA256SUMS.txt"
because 'twice'
assets_case 1 'a file listed twice' "head -n 1 SHA256SUMS.txt >> SHA256SUMS.txt"
because 'does not list extra.txt'
assets_case 1 'an asset not listed' "printf x > extra.txt"
because 'which is not an asset'
assets_case 1 'a listed file that is not an asset' "rm -- '$zip'"
because 'does not match its digest'
assets_case 1 'a wrong digest' "printf changed > '$setup'"
because "the asset name 'Read me.txt' is not allowed"
assets_case 1 'an asset name with a space' "printf x > 'Read me.txt'; echo \"\$(sha256sum < 'Read me.txt' | cut -c 1-64)  Read me.txt\" >> SHA256SUMS.txt"
because "the asset name 'notes..txt' is not allowed"
assets_case 1 "an asset name with '..'" "printf x > 'notes..txt'; echo \"\$(sha256sum < notes..txt | cut -c 1-64)  notes..txt\" >> SHA256SUMS.txt"
because 'sub is not a plain file'
assets_case 1 'a folder among the assets' 'mkdir sub'
because 'SHA256SUMS.txt is larger than 65536 bytes'
assets_case 1 'a checksum file over 64 KiB' "head -c 65537 /dev/zero | tr '\\0' 'a' > pad; cat pad >> SHA256SUMS.txt; rm pad"
because 'is larger than 536870912 bytes'
assets_case 1 'an installer over 512 MiB' "truncate -s 536870913 '$setup'"
because 'is not <major>.<minor>.<patch>'
expect 1 'assets of a version with a leading zero' bash "$check" assets "$assets" 0.01.0
fresh_assets
because 'the installer ParadoxModManager-0.1.1-windows-x86_64-setup.exe is missing'
expect 1 'assets of another version' bash "$check" assets "$assets" 0.1.1

if [ "$failures" -ne 0 ]; then
  echo "test-update-contract: $failures of $count checks failed" >&2
  exit 1
fi
echo "test-update-contract: $count checks passed"
