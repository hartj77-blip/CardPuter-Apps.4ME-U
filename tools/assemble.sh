#!/usr/bin/env bash
# Put a release together: the packages of every level built now, and one
# zeroos-apps.index that lists them all (README.md, "Compatibility").
# Builds for the level just below, which devices that haven't updated ZeroOS
# yet still need, are carried over from the previous release.
#
# Usage: tools/assemble.sh <out-dir> <built-for> <level-dir>...
#   built-for: "release=v0.3.4 levels=3,4" (tools/levels.sh), kept in the index
#   level-dir: a tools/build.sh out-dir (its packages and index)
# Env: PREVIOUS: a folder with the previous release's files (optional)
set -euo pipefail
OUT=$(realpath -m "$1")
BUILT_FOR=$2
shift 2
[ $# -gt 0 ] || { echo "usage: tools/assemble.sh <out-dir> <built-for> <level-dir>..." >&2; exit 1; }
mkdir -p "$OUT"
rm -f -- "${OUT:?}"/zeroos-app-*.deb "${OUT:?}/zeroos-apps.index"

# blocks <index> <abi>: the index's blocks for one level.
blocks() {
    awk -v RS= -v ORS='\n\n' -v want="abi=$2" '
        /^app=/ { n = split($0, l, "\n"); for (i = 2; i <= n; i++) if (l[i] == want) { print; break } }' "$1"
}

lowest=""
{
    echo "# ZeroOS apps. Read by zeroos-apps on the device."
    echo "# built-for $BUILT_FOR"
    echo
    for dir in "$@"; do
        abi=$(sed -n 's/^# level abi=\([0-9]*\) .*/\1/p' "$dir/zeroos-apps.index")
        [ -n "$abi" ] || { echo "error: $dir/zeroos-apps.index names no level" >&2; exit 1; }
        if [ -z "$lowest" ] || [ "$abi" -lt "$lowest" ]; then lowest=$abi; fi
        grep '^# level ' "$dir/zeroos-apps.index"
        echo
        blocks "$dir/zeroos-apps.index" "$abi"
        cp "$dir"/zeroos-app-*.deb "$OUT/"
    done
    old=$((lowest - 1))
    if [ -n "${PREVIOUS:-}" ] && [ -f "$PREVIOUS/zeroos-apps.index" ] && [ "$old" -gt 0 ]; then
        carried=$(blocks "$PREVIOUS/zeroos-apps.index" "$old")
        if [ -n "$carried" ]; then
            echo "# level abi=$old carried over from the previous release"
            echo
            printf '%s\n\n' "$carried"
            for f in $(printf '%s\n' "$carried" | sed -n 's/^file=\(zeroos-app-[a-z0-9-]*_[0-9A-Za-z.+~-]*_[a-z0-9]*\.deb\)$/\1/p'); do
                [ -f "$PREVIOUS/$f" ] || { echo "error: the previous release lists $f but doesn't hold it" >&2; exit 1; }
                cp "$PREVIOUS/$f" "$OUT/"
            done
        fi
    fi
} > "$OUT/zeroos-apps.index.tmp"
mv -f "$OUT/zeroos-apps.index.tmp" "$OUT/zeroos-apps.index"

# Every package the index lists is here, whole.
awk -F= '$1 == "file" {f = $2} $1 == "sha256" {print $2 "  " f}' "$OUT/zeroos-apps.index" > "$OUT/.sums"
(cd "$OUT" && sha256sum --quiet -c .sums)
rm -f -- "$OUT/.sums"
ls -l "$OUT" >&2
