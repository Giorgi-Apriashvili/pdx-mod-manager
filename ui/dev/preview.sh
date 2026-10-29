#!/usr/bin/env bash
# Runs the QML UI against the fake backend in ui/dev/Pmm/Backend with Qt's qml
# tool. No C++ build is needed: a preview-only qmldir that points at the source
# files is generated under build/ui-preview, so edits show up on the next run.
#
#   ui/dev/preview.sh                      interactive window
#   ui/dev/preview.sh --screenshot [DIR]   render PNGs (default docs/screenshots) and quit
#   ui/dev/preview.sh --lint               run qmllint on every QML file
#
# Finds the Qt tools through QT_ROOT_DIR (e.g. C:/Qt/6.10.3/msvc2022_64) or PATH.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ui="$(cd "$here/.." && pwd)"
repo="$(cd "$ui/.." && pwd)"
stage="$repo/build/ui-preview"
module="$stage/Pmm/Ui"

tool() {
    if [ -n "${QT_ROOT_DIR:-}" ] && [ -e "$QT_ROOT_DIR/bin/$1" -o -e "$QT_ROOT_DIR/bin/$1.exe" ]; then
        echo "$QT_ROOT_DIR/bin/$1"
    elif command -v "$1" >/dev/null 2>&1; then
        command -v "$1"
    else
        echo "preview: '$1' not found; set QT_ROOT_DIR to your Qt 6 kit" >&2
        exit 1
    fi
}

# qmldir listing every QML_FILES entry of ui/CMakeLists.txt relative to $module.
mkdir -p "$module"
{
    echo "module Pmm.Ui"
    sed -n '/QML_FILES/,/^)/p' "$ui/CMakeLists.txt" | grep -E '\.(qml|js)[[:space:]]*$' |
        sed 's/^[[:space:]]*//; s/[[:space:]]*$//' |
        while read -r file; do
            name="$(basename "${file%.*}")"
            prefix=""
            [ "$name" = "Theme" ] && prefix="singleton "
            echo "${prefix}${name} 1.0 ../../../../ui/$file"
        done
} > "$module/qmldir"

# Qt sends QML warnings to the debugger output on Windows unless told otherwise.
export QT_FORCE_STDERR_LOGGING=1

case "${1:-}" in
    --screenshot)
        out="${2:-$repo/docs/screenshots}"
        mkdir -p "$out"
        # Fixed scale so the PNGs are exactly 1400x860 regardless of the display.
        export QT_ENABLE_HIGHDPI_SCALING=0 QT_SCALE_FACTOR=1
        "$(tool qml)" -I "$stage" -I "$here" "$here/screenshot.qml" -- "--out=$(cd "$out" && pwd)"
        ;;
    --test)
        "$(tool qmltestrunner)" -import "$stage" -import "$here" -input "$here/tests"
        ;;
    --lint)
        status=0
        while IFS= read -r -d '' file; do
            "$(tool qmllint)" -I "$stage" -I "$here" "$file" || status=1
        done < <(find "$ui" -path "$ui/dev" -prune -o -name '*.qml' -print0)
        exit $status
        ;;
    *)
        "$(tool qml)" -I "$stage" -I "$here" "$here/preview.qml"
        ;;
esac
