#!/usr/bin/env bash
# Checks the update contract, frozen since 0.1.0: packaging/update-contract.txt,
# explained in docs/updates.md ("Compatibility contract").
#
#   scripts/check-update-contract.sh                       the sources still carry every value (CI),
#                                                          in their code: comments do not count
#   scripts/check-update-contract.sh version <x.y.z>       a version a release may have
#   scripts/check-update-contract.sh published <x.y.z>     the same, and strictly higher than every
#                                                          published release (gh; GH_REPO or GITHUB_REPOSITORY)
#   scripts/check-update-contract.sh assets <dir> <x.y.z>  the files of a release, before it is created
#   scripts/check-update-contract.sh get <key>             prints one value of the contract
#
# Every difference is printed; the exit code is 1 if there is one, 2 for wrong use.
set -euo pipefail
shopt -s inherit_errexit
export LC_ALL=C

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ -n "$root" ] || exit 2
contract="$root/packaging/update-contract.txt"
problems=0
tmp_dir=""
trap '[ -z "$tmp_dir" ] || rm -rf -- "$tmp_dir"' EXIT

die() {
  echo "check-update-contract: $1" >&2
  exit 2
}

problem() {
  echo "check-update-contract: $1" >&2
  problems=$((problems + 1))
}

finish() {
  if [ "$problems" -ne 0 ]; then
    echo "check-update-contract: $problems problem(s); the contract is in packaging/update-contract.txt" >&2
    exit 1
  fi
  echo "check-update-contract: $1"
}

# The value of <key>: exactly one "<key> = <value>" line.
value() {
  local found
  [ -f "$contract" ] || die "$contract is missing"
  found="$(awk -v key="$1" '
    { sub(/\r$/, "") }
    index($0, key " = ") == 1 { print substr($0, length(key) + 4); count++ }
    END { exit count == 1 ? 0 : 1 }' "$contract")" ||
    die "packaging/update-contract.txt has no single value for '$1'"
  [ -n "$found" ] || die "packaging/update-contract.txt has an empty value for '$1'"
  printf '%s\n' "$found"
}

# The parts of a valid version, separated by spaces; nothing for an invalid one.
version_parts() {
  local regex max part
  regex="$(value version_regex)"
  max="$(value version_part_max)"
  [ -n "$regex" ] && [[ "$max" =~ ^[0-9]+$ ]] || die "version_regex or version_part_max is not usable"
  [[ "$1" =~ $regex ]] || return 0
  local parts=()
  IFS=. read -r -a parts <<< "$1"
  [ "${#parts[@]}" -eq 3 ] || return 0
  for part in "${parts[@]}"; do
    [[ "$part" =~ ^[0-9]+$ ]] || return 0
    # The length first: bash arithmetic overflows on long numbers.
    if [ "${#part}" -gt "${#max}" ] || [ "$part" -gt "$max" ]; then
      return 0
    fi
  done
  echo "${parts[*]}"
}

check_version() {
  if [ -z "$(version_parts "$1")" ]; then
    problem "version '$1' is not <major>.<minor>.<patch> (decimal, no leading zeros, each part at most $(value version_part_max))"
    return 1
  fi
}

# Whether version $1 is higher than version $2; both valid.
version_greater() {
  local a b i
  read -r -a a <<< "$(version_parts "$1")"
  read -r -a b <<< "$(version_parts "$2")"
  for i in 0 1 2; do
    if [ "${a[i]}" -ne "${b[i]}" ]; then
      [ "${a[i]}" -gt "${b[i]}" ]
      return
    fi
  done
  return 1
}

# ---- sources -------------------------------------------------------------

# The checks read the code of a source, without its comments: a value that is left
# only in a comment is a drift. Documentation is read whole (documents).
passed=0
code_dir=""
list=()

# strip_comments <file>: the code of the file, by the comment syntax of its type:
# every comment removed (one that ends within a line leaves a space), every string
# kept whole, every line on its line number. CRs are dropped, as grep on Windows
# drops them. POSIX awk: mawk on the Ubuntu runners.
#   C++            // and /* */, outside "...", '...' and R"x(...)x"
#   Inno Setup     lines starting with // everywhere (the preprocessor drops them),
#                  and with ";" except in [Code]; there Pascal's //, { } and (* *),
#                  outside '...' and {#...}; preprocessor lines (#) are code
#   PowerShell     # where a token starts, and <# #>, outside '...', "..." and
#                  here-strings
#   YAML           # at the start of a line or after a blank, outside '...' and
#                  "..." within the line: YAML comments and those of run: scripts
#   CMake          # and #[[ ]], outside "..." and [[ ]] (any number of =)
#   Markdown       read whole
# Not handled: a quote within $(...) in a PowerShell "..." string ends the string,
# and a C++ // comment does not go on after a backslash at the end of its line.
strip_comments() {
  local syntax
  case "$1" in
    *.hpp | *.cpp) syntax=cpp ;;
    *.iss) syntax=inno ;;
    *.ps1) syntax=powershell ;;
    *.yml) syntax=yaml ;;
    CMakeLists.txt | */CMakeLists.txt | *.cmake) syntax=cmake ;;
    *.md) syntax=text ;;
    *) die "no comment syntax is known for $1" ;;
  esac
  awk -v syntax="$syntax" -v q="'" '
    # mode: "" in code; "comment" or "string" until the text in closer, which can
    # be on a later line; "here" in a PowerShell here-string, until a line starts
    # with closer. In a string, escape is its escape character ("" for none),
    # doubled whether a doubled closing quote stays in it (Pascal, PowerShell), and
    # multiline whether it goes on past the end of its line.
    function enter_string(text, esc, dbl, multi) {
      mode = "string"; closer = text; escape = esc; doubled = dbl; multiline = multi
    }
    # The identifier or number that ends right before position at of s.
    function word_before(s, at,   start) {
      start = at
      while (start > 1 && substr(s, start - 1, 1) ~ /[A-Za-z0-9_]/) start--
      return substr(s, start, at - start)
    }
    BEGIN {
      mode = ""; section = ""
      if (syntax == "cpp") special = "[/\"" q "]"
      else if (syntax == "inno") special = "[/{(" q "]"
      else if (syntax == "powershell") special = "[#<@`\"" q "]"
      else if (syntax == "yaml") special = "[#\\\\\"" q "]"
      else special = "[#\"\\\\[]"
      here_start = "^[\"" q "][ \t]*$"
    }
    {
      sub(/\r$/, "")
      line = $0; n = length(line); out = ""; i = 1
      if (syntax == "text") { print line; next }
      if (syntax == "inno") {
        if (line ~ /^[ \t]*\[[A-Za-z]+][ \t]*$/) {
          section = tolower(line); gsub(/[^a-z]/, "", section); mode = ""
          print line; next
        }
        if (line ~ /^[ \t]*#/) { print line; next }
        # The preprocessor drops the line, also within a Pascal comment.
        if (line ~ /^[ \t]*\/\//) { print ""; next }
        if (section != "code") {
          if (line ~ /^[ \t]*;/) line = ""
          print line; next
        }
      }
      if (mode == "here") {
        lead = line; sub(/[^ \t].*$/, "", lead)
        if (substr(line, length(lead) + 1, 2) != closer) { print line; next }
        i = length(lead) + 3; out = substr(line, 1, i - 1); mode = ""
      }
      while (i <= n) {
        rest = substr(line, i)
        if (mode == "comment") {
          j = index(rest, closer)
          # The preprocessor expands {#...} in a Pascal { } comment too.
          if (closer == "}" && (k = index(rest, "{#")) > 0 && k < j) { i += j; continue }
          if (j == 0) break
          i += j - 1 + length(closer); out = out " "; mode = ""
          continue
        }
        if (mode == "string") {
          j = index(rest, closer)
          k = (escape == "") ? 0 : index(rest, escape)
          if (k > 0 && (j == 0 || k < j)) { out = out substr(rest, 1, k + 1); i += k + 1; continue }
          if (j == 0) { out = out rest; break }
          j += length(closer) - 1
          if (doubled && substr(rest, j + 1, 1) == closer) j++
          else mode = ""
          out = out substr(rest, 1, j); i += j
          continue
        }
        if (!match(rest, special)) { out = out rest; break }
        out = out substr(rest, 1, RSTART - 1); i += RSTART - 1
        c = substr(line, i, 1); c2 = substr(line, i, 2)
        prev = (i > 1) ? substr(line, i - 1, 1) : ""
        if (syntax == "cpp") {
          if (c2 == "//") break
          if (c2 == "/*") { mode = "comment"; closer = "*/"; i += 2; continue }
          if (c == q) {
            # After a number, a quote is a digit separator.
            if (word_before(line, i) !~ /^[0-9]/) enter_string(q, "\\", 0, 0)
          } else if (c == "\"") {
            if (word_before(line, i) ~ /^(u8|[uUL])?R$/ && match(substr(line, i + 1), /^[^ ()\\\t]*[(]/)) {
              enter_string(")" substr(line, i + 1, RLENGTH - 1) "\"", "", 0, 1)
              out = out substr(line, i, RLENGTH + 1); i += RLENGTH + 1
              continue
            }
            enter_string("\"", "\\", 0, 0)
          }
        } else if (syntax == "inno") {
          if (c2 == "//") break
          if (c2 == "{#") {
            j = index(substr(line, i), "}")
            if (j == 0) j = n - i + 1
            out = out substr(line, i, j); i += j
            continue
          }
          if (c == "{") { mode = "comment"; closer = "}"; i++; continue }
          if (c2 == "(*") { mode = "comment"; closer = "*)"; i += 2; continue }
          if (c == q) enter_string(q, "", 1, 0)
        } else if (syntax == "powershell") {
          if (c2 == "<#") { mode = "comment"; closer = "#>"; i += 2; continue }
          if (c == "#" && (prev == "" || prev ~ /[ \t;,=|&(){}]/)) break
          if (c == "`") { out = out c2; i += 2; continue }
          if (c == "@" && substr(line, i + 1) ~ here_start) {
            mode = "here"; closer = substr(line, i + 1, 1) "@"; out = out substr(line, i)
            break
          }
          if (c == q) enter_string(q, "", 1, 1)
          else if (c == "\"") enter_string("\"", "`", 1, 1)
        } else if (syntax == "yaml") {
          if (c == "#" && (prev == "" || prev ~ /[ \t]/)) break
          if (c == "\\") { out = out c2; i += 2; continue }
          if (c == q) enter_string(q, "", 0, 0)
          else if (c == "\"") enter_string("\"", "\\", 0, 0)
        } else {
          if (c == "#") {
            if (!match(substr(line, i + 1), /^\[=*\[/)) break
            mode = "comment"; closer = "]" substr(line, i + 2, RLENGTH - 2) "]"; i += RLENGTH + 1
            continue
          }
          if (c == "\\") { out = out c2; i += 2; continue }
          if (c == "[" && (prev == "" || prev ~ /[ \t(]/) && match(substr(line, i), /^\[=*\[/)) {
            enter_string("]" substr(line, i + 1, RLENGTH - 2) "]", "", 0, 1)
            out = out substr(line, i, RLENGTH); i += RLENGTH
            continue
          }
          if (c == "\"") enter_string("\"", "\\", 0, 1)
        }
        out = out c; i++
      }
      # A string that cannot span lines ends with its line, unless the line ends
      # with a backslash (C++ and YAML).
      if (mode == "string" && !multiline && substr(line, n, 1) != "\\") mode = ""
      print out
    }' < "$root/$1"
}

# readable <code|text> <file>...: sets list to what the checks read of the files:
# their code, a copy without comments (strip_comments) made once in $code_dir, or
# their whole text. Returns 1 for a missing file, which is a problem.
readable() {
  local kind="$1" file
  shift
  list=()
  for file in "$@"; do
    if [ ! -f "$root/$file" ]; then
      problem "$file is missing"
      return 1
    fi
    if [ "$kind" = text ]; then
      list+=("$root/$file")
      continue
    fi
    if [ ! -f "$code_dir/$file" ]; then
      mkdir -p "$code_dir/$(dirname "$file")"
      strip_comments "$file" > "$code_dir/$file"
    fi
    list+=("$code_dir/$file")
  done
}

# contains <code|text> <description> <fixed text> <file>...
contains() {
  local kind="$1" what="$2" text="$3"
  shift 3
  readable "$kind" "$@" || return 0
  if grep -F -q -e "$text" -- "${list[@]}"; then
    passed=$((passed + 1))
  else
    problem "drift: $what; expected in $*: $text"
  fi
}

# has <description> <fixed text> <file>...: the code of one of the files contains
# the text.
has() {
  contains code "$@"
}

# documents <description> <fixed text> <file>...: one of the files contains the
# text, comments included: documentation of a value.
documents() {
  contains text "$@"
}

# has_regex <description> <extended regex> <file>...
has_regex() {
  local what="$1" regex="$2"
  shift 2
  readable code "$@" || return 0
  if grep -E -q -e "$regex" -- "${list[@]}"; then
    passed=$((passed + 1))
  else
    problem "drift: $what; expected in $*: /$regex/"
  fi
}

# lacks <description> <extended regex, case-insensitive> <file>...
lacks() {
  local what="$1" regex="$2" found
  shift 2
  readable code "$@" || return 0
  if found="$(grep -E -i -n -H -e "$regex" -- "${list[@]}")"; then
    problem "drift: $what; found: ${found//$code_dir\//}"
  else
    passed=$((passed + 1))
  fi
}

# The value of a constant such as "kMaxInstallerBytes = 512ll * 1024 * 1024;" in
# the code of a file, computed; nothing when it is not a plain product.
cpp_constant() {
  local expression
  readable code "$2" || return 0
  expression="$(sed -n "s/.*$1 = \\(.*\\);.*/\\1/p" "${list[0]}" | head -n 1)"
  expression="$(printf '%s' "$expression" | sed -e 's/std::size_t{\([0-9]*\)}/\1/g' -e 's/\([0-9]\)ll/\1/g')"
  [[ "$expression" =~ ^[0-9]+( \* [0-9]+)*$ ]] || return 0
  echo $((expression))
}

check_sources() {
  local api_url id api_host repository tag installer prefix suffix checksums
  local arguments mutex app_id scope executable marker tls
  api_url="$(value api_url)"
  id="$(value repository_id)"
  repository="$(value repository)"
  tag="$(value tag)"
  installer="$(value installer_asset)"
  checksums="$(value checksum_asset)"
  arguments="$(value installer_arguments)"
  mutex="$(value running_mutex)"
  app_id="$(value app_id)"
  scope="$(value install_scope)"
  executable="$(value app_executable)"
  marker="$(value installed_marker)"
  tls="$(value tls_plugin)"

  # The contract itself.
  [[ "$id" =~ ^[1-9][0-9]*$ ]] || problem "repository_id '$id' is not a number"
  [[ "$api_url" =~ ^https://([a-z.]+)/repositories/$id/releases/latest$ ]] ||
    problem "api_url '$api_url' is not https://<host>/repositories/$id/releases/latest"
  api_host="${BASH_REMATCH[1]:-}"
  [ "$(value authentication)" = none ] || problem "authentication must stay 'none'"
  [ "$(value release_channel)" = releases/latest ] || problem "release_channel must stay 'releases/latest'"
  [ "$tag" = 'v<version>' ] || problem "tag must stay 'v<version>'"
  [ "$scope" = per-user ] || problem "install_scope must stay 'per-user'"
  [[ "$installer" == *'<version>'* ]] || problem "installer_asset has no <version>"
  prefix="${installer%%<version>*}"
  suffix="${installer#*<version>}"
  [ "$(value checksum_line_regex)" = "^[0-9a-f]{64}  $(value asset_name_regex | sed 's/^\^//')" ] ||
    problem "checksum_line_regex and asset_name_regex name different file names"
  local good bad
  for good in 0.0.0 0.1.0 1.2.3 10.20.30 65535.65535.65535; do
    [ -n "$(version_parts "$good")" ] || problem "version_regex rejects $good"
  done
  for bad in 01.0.0 0.01.0 0.1 0.1.0.0 v0.1.0 0.1.0-rc1 ' 0.1.0' 65536.0.0 -1.0.0; do
    [ -z "$(version_parts "$bad")" ] || problem "version_regex accepts '$bad'"
  done

  tmp_dir="$(mktemp -d)"
  [ -n "$tmp_dir" ] && [ -d "$tmp_dir" ] || die "mktemp failed"
  code_dir="$tmp_dir/code"
  local cmake=CMakeLists.txt
  local update=(src/services/app_update.hpp src/services/app_update.cpp)
  local client=(src/app/core/update_client.hpp src/app/core/update_client.cpp)
  local instance=(src/app/core/running_instance.hpp src/app/core/running_instance.cpp)
  local iss=packaging/windows/installer.iss
  local smoke=packaging/windows/smoke-test-installer.ps1
  local release=.github/workflows/release.yml
  local ci=.github/workflows/ci.yml
  local prune=packaging/windows/prune-qt-deployment.cmake
  local doc=docs/updates.md

  # Repository identity: the id in CMake, the request in the code that builds it.
  has_regex "PMM_GITHUB_REPOSITORY_ID is $id" \
    "set\\([[:space:]]*PMM_GITHUB_REPOSITORY_ID[[:space:]]+\"?$id\"?[[:space:]]*\\)" "$cmake"
  has "HOMEPAGE_URL names $repository" "HOMEPAGE_URL \"https://github.com/$repository\"" "$cmake"
  has "the API host is $api_host" "\"$api_host\"" "${update[@]}"
  has "the API request uses the repository id" "/repositories/" "${update[@]}"
  has "the API request asks for releases/latest" "/releases/latest" "${update[@]}"
  has_regex "the code that builds the request gets the id" \
    "PMM_GITHUB_REPOSITORY_ID|(^|[^0-9])$id([^0-9]|$)" "${update[@]}"
  has "the client requests that URL" "latestReleaseApiUrl(" "${client[@]}"
  # A header name in quotes or Qt's enum value, not a word in a string.
  lacks "the client sends no credentials" \
    '"(proxy-)?authorization"|"(bearer|token|basic) |WellKnownHeader::(Proxy)?Authorization' "${client[@]}"
  has "drafts are recognised" '"draft"' "${update[@]}"
  has "pre-releases are recognised" '"prerelease"' "${update[@]}"

  # Assets and limits.
  has "the installer name starts with $prefix" "\"$prefix\"" "${update[@]}"
  has "the installer name ends with $suffix" "\"$suffix\"" "${update[@]}"
  has "the checksum file is $checksums" "\"$checksums\"" "${update[@]}"
  [ "$(cpp_constant kMaxInstallerBytes src/services/app_update.hpp)" = "$(value max_installer_bytes)" ] ||
    problem "drift: kMaxInstallerBytes in src/services/app_update.hpp is not $(value max_installer_bytes)"
  [ "$(cpp_constant kMaxChecksumFileBytes src/services/app_update.hpp)" = "$(value max_checksum_bytes)" ] ||
    problem "drift: kMaxChecksumFileBytes in src/services/app_update.hpp is not $(value max_checksum_bytes)"

  # The update run.
  local words=() cpp_args ps_args word param_name param_value
  read -r -a words <<< "$arguments"
  cpp_args="{"
  ps_args="@("
  for word in "${words[@]}"; do
    cpp_args+="\"$word\", "
    ps_args+="'$word', "
  done
  cpp_args="${cpp_args%, }}"
  ps_args="${ps_args%, })"
  param_name="${words[${#words[@]} - 1]#/}"
  param_value="${param_name#*=}"
  param_name="${param_name%%=*}"
  has "the application starts the setup with $arguments" "$cpp_args" "${update[@]}"
  documents "installer.iss documents $arguments" "$arguments" "$iss"
  has "installer.iss recognises /$param_name=$param_value" \
    "ExpandConstant('{param:$param_name|0}') = '$param_value'" "$iss"
  has "the smoke test runs the setup with $arguments" "$ps_args" "$smoke"
  has "the application holds $mutex" "\"${mutex//\\/\\\\}\"" "${instance[@]}"
  has "installer.iss waits for $mutex" "#define AppRunningMutex \"$mutex\"" "$iss"
  has "installer.iss checks AppMutex" "AppMutex={#AppRunningMutex}" "$iss"
  has "installer.iss polls the mutex" "CheckForMutexes('{#AppRunningMutex}')" "$iss"
  has_regex "installer.iss starts the application again after an update run" \
    '^[[:space:]]*Filename:[[:space:]]*"\{app\}\\\{#AppExe\}";(.*;)?[[:space:]]*Check:[[:space:]]*IsPmmUpdate[[:space:]]*(;|$)' "$iss"
  has "the smoke test holds $mutex" "'$mutex'" "$smoke"

  # The installation.
  has "installer.iss has AppId $app_id" "AppId={$app_id" "$iss"
  has "installer.iss reads the uninstall entry of $app_id" "\\Uninstall\\${app_id}_is1" "$iss"
  has "the smoke test uses the uninstall entry of $app_id" "\$appId = '${app_id}_is1'" "$smoke"
  has "the installation is per-user" "PrivilegesRequired=lowest" "$iss"
  lacks "the installation cannot be switched to all users" \
    "^[[:space:]]*PrivilegesRequiredOverridesAllowed[[:space:]]*=" "$iss"
  has "the application is $executable" "#define AppExe \"$executable\"" "$iss"
  has "the smoke test expects $executable" "'$executable'" "$smoke"
  lacks "the uninstaller stays in the installation folder ($marker)" \
    "^[[:space:]]*UninstallFilesDir[[:space:]]*=" "$iss"
  has "the application detects an installation by $marker" \
    "/ \"${marker%%\\*}\" / \"${marker#*\\}\"" "${update[@]}"
  has "the smoke test expects ${marker#*\\}" "'${marker#*\\}'" "$smoke"
  has "the setup is named after the asset" "OutputBaseFilename=$prefix{#AppVersion}${suffix%.exe}" "$iss"
  local tls_in_plugins="${tls#*\\}"
  has "packages must have $tls" "/${tls_in_plugins//\\//}\"" "$prune"
  has "the smoke test expects $tls" "'$tls'" "$smoke"

  # The release workflow and CI.
  has "the workflow builds the asset names" "$prefix\$VERSION$suffix" "$release"
  has "the workflow writes $checksums" "$checksums" "$release"
  has "the workflow checks the version" "scripts/check-update-contract.sh version" "$release"
  has "the workflow checks the published releases" "scripts/check-update-contract.sh published" "$release"
  has "the workflow checks the assets" "scripts/check-update-contract.sh assets" "$release"
  has "the workflow checks the id in pmm-app.exe" "check-update-contract.sh get repository_id" "$release"
  has "the workflow runs in the repository the application asks" "GITHUB_REPOSITORY_ID" "$release"
  has "CI runs this check" "scripts/check-update-contract.sh" "$ci"

  # The documentation names every frozen value.
  local key
  for key in api_url installer_asset checksum_asset installer_arguments running_mutex app_id \
    app_executable installed_marker tls_plugin version_regex checksum_line_regex; do
    documents "docs/updates.md names $key" "$(value "$key")" "$doc"
  done

  finish "$passed source checks OK"
}

# ---- releases -------------------------------------------------------------

check_published() {
  local version="$1" repo latest tag prerelease
  check_version "$version" || finish ""
  repo="${GH_REPO:-${GITHUB_REPOSITORY:-}}"
  [ -n "$repo" ] || die "set GH_REPO or GITHUB_REPOSITORY"
  command -v gh > /dev/null || die "gh is not installed"
  tmp_dir="$(mktemp -d)"
  [ -n "$tmp_dir" ] && [ -d "$tmp_dir" ] || die "mktemp failed"

  # Drafts are not releases yet (and are not listed for a read-only token).
  gh api --paginate "repos/$repo/releases" \
    --jq '.[] | select(.draft | not) | "\(.tag_name) \(.prerelease)"' > "$tmp_dir/releases"
  # 404: nothing published yet, or only pre-releases.
  if gh api "repos/$repo/releases/latest" --jq .tag_name > "$tmp_dir/latest" 2> "$tmp_dir/latest.err"; then
    latest="$(tr -d '\r' < "$tmp_dir/latest")"
  elif grep -q 'HTTP 404' "$tmp_dir/latest.err"; then
    latest=""
  else
    cat "$tmp_dir/latest.err" >&2
    die "could not read the latest release of $repo"
  fi

  while read -r tag prerelease; do
    [ -n "$tag" ] || continue
    if [ "$tag" = "v$version" ]; then
      problem "release $tag already exists"
    elif [ -z "$(version_parts "${tag#v}")" ] || [ "${tag#v}" = "$tag" ]; then
      echo "check-update-contract: ignoring release $tag, which is not v<x.y.z>" >&2
    elif ! version_greater "$version" "${tag#v}"; then
      problem "version $version is not higher than the published release $tag"
    fi
    if [ -z "$latest" ] && [ "$prerelease" = false ]; then
      problem "GitHub reports no latest release, but $tag is published and not a pre-release"
    fi
  done < <(tr -d '\r' < "$tmp_dir/releases")

  if [ -n "$latest" ]; then
    if [ "${latest#v}" = "$latest" ] || [ -z "$(version_parts "${latest#v}")" ]; then
      problem "the latest release has the tag '$latest', which is not v<x.y.z>"
    elif ! version_greater "$version" "${latest#v}"; then
      problem "version $version is not higher than the latest release $latest"
    fi
    finish "version $version is higher than the latest release $latest and every published release"
  else
    finish "version $version: no release has been published yet, and it is higher than every pre-release"
  fi
}

file_size() {
  wc -c < "$1" | tr -d ' '
}

check_assets() {
  local dir="$1" version="$2" installer checksums line_regex name_regex
  [ -d "$dir" ] || die "$dir is not a folder"
  check_version "$version" || finish ""
  installer="$(value installer_asset)"
  installer="${installer//<version>/$version}"
  checksums="$(value checksum_asset)"
  line_regex="$(value checksum_line_regex)"
  name_regex="$(value asset_name_regex)"

  local path name
  local -A on_disk=()
  while IFS= read -r -d '' path; do
    name="${path##*/}"
    if [ ! -f "$path" ] || [ -L "$path" ]; then
      problem "$name is not a plain file"
      continue
    fi
    if ! [[ "$name" =~ $name_regex ]] || [[ "$name" == *..* ]]; then
      problem "the asset name '$name' is not allowed: $name_regex, without '..'"
    fi
    on_disk["$name"]=1
  done < <(find "$dir" -mindepth 1 -maxdepth 1 -print0)

  [ -n "${on_disk[$installer]:-}" ] || problem "the installer $installer is missing"
  if [ -z "${on_disk[$checksums]:-}" ]; then
    problem "$checksums is missing"
    finish ""
  fi
  if [ -n "${on_disk[$installer]:-}" ] && [ "$(file_size "$dir/$installer")" -gt "$(value max_installer_bytes)" ]; then
    problem "$installer is larger than $(value max_installer_bytes) bytes"
  fi
  local sums="$dir/$checksums"
  if [ "$(file_size "$sums")" -gt "$(value max_checksum_bytes)" ]; then
    problem "$checksums is larger than $(value max_checksum_bytes) bytes"
    finish ""
  fi
  [ "$(head -c 3 "$sums" | od -An -tx1 | tr -d ' \n')" != efbbbf ] || problem "$checksums starts with a BOM"
  # Not grep: on Windows it drops the CR of CRLF before matching.
  [ "$(tr -cd '\r' < "$sums" | wc -c | tr -d ' ')" = 0 ] ||
    problem "$checksums has CR characters; lines must end with LF only"
  [ -s "$sums" ] || problem "$checksums is empty"
  [ ! -s "$sums" ] || [ "$(tail -c 1 "$sums" | od -An -tx1 | tr -d ' \n')" = 0a ] ||
    problem "the last line of $checksums does not end with LF"

  local line number=0 listed_name
  local -A listed=()
  while IFS= read -r line || [ -n "$line" ]; do
    number=$((number + 1))
    line="${line%$'\r'}"
    if ! [[ "$line" =~ $line_regex ]]; then
      problem "line $number of $checksums is not '<64 lowercase hex>  <name>': '$line'"
      continue
    fi
    listed_name="${line:66}"
    [[ "$listed_name" != *..* ]] || problem "line $number of $checksums names '$listed_name', which has '..'"
    [ -z "${listed[$listed_name]:-}" ] || problem "$checksums lists $listed_name twice"
    listed["$listed_name"]=1
    [ -n "${on_disk[$listed_name]:-}" ] || problem "$checksums lists $listed_name, which is not an asset"
  done < "$sums"

  for name in "${!on_disk[@]}"; do
    [ "$name" = "$checksums" ] || [ -n "${listed[$name]:-}" ] || problem "$checksums does not list $name"
  done
  [ -z "${listed[$checksums]:-}" ] || problem "$checksums lists itself"

  if [ "$problems" -eq 0 ]; then
    (cd "$dir" || exit 1; sha256sum --check --strict --quiet -- "$checksums") ||
      problem "an asset does not match its digest in $checksums"
  fi
  finish "the assets of $version follow the contract (${#on_disk[@]} files, $number checksums)"
}

case "${1:-}" in
  '' | sources)
    [ "$#" -le 1 ] || die "usage: $0 [sources]"
    check_sources
    ;;
  version)
    [ "$#" -eq 2 ] || die "usage: $0 version <x.y.z>"
    check_version "$2" || true
    finish "version $2 follows the contract"
    ;;
  published)
    [ "$#" -eq 2 ] || die "usage: $0 published <x.y.z>"
    check_published "$2"
    ;;
  assets)
    [ "$#" -eq 3 ] || die "usage: $0 assets <dir> <x.y.z>"
    check_assets "$2" "$3"
    ;;
  get)
    [ "$#" -eq 2 ] || die "usage: $0 get <key>"
    value "$2"
    ;;
  *)
    die "unknown command '$1'; see the comment at the top of $0"
    ;;
esac
