#!/usr/bin/env bash
# Pack every app: one zeroos-app-<id>_<version>-abi<n>_arm64.deb per folder in
# apps/, and zeroos-apps.index, which the Apps app on the device reads from
# this repository's newest release (README.md, "Compatibility").
#
# Usage: tools/build.sh [build-dir] [out-dir] [app-id...]
#   build-dir: cmake -S . -B <dir> -DZS_BACKEND=device   (default build/device)
#   out-dir:   where the packages and the index go        (default build/apps)
#   app-id:    only these apps (default: all)
# Env:   ZEROOS_ARCH (default arm64)
# Needs: dpkg-deb; curl, gpg, python3 and git for the apps with a prepare.sh.
#
# An app's folder holds:
#   <id>.pkg     NAME, SUMMARY, APP_VERSION, DEPENDS... and payload() (base/image/apps/make-app-deb.sh)
#   files/       what payload() copies from, as $F
#   prepare.sh   optional: fetches or builds what payload() takes from $WORK
# payload() also sees $ZEROOS_BUILD, the build dir (programs in its bin/).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$(realpath "${1:-$ROOT/build/device}")
OUT=$(realpath -m "${2:-$ROOT/build/apps}")
shift $(( $# > 2 ? 2 : $# ))
BASE=${ZEROOS_BASE:-$ROOT/base}
ABI=$(sed -n 's/^#define ZS_APP_ABI \([0-9]*\).*/\1/p' "$BASE/shell/src/ui/app.h" 2>/dev/null || true)
[ -n "$ABI" ] || { echo "error: no ZS_APP_ABI in $BASE/shell/src/ui/app.h (git submodule update --init base)" >&2; exit 1; }
OS=$(git -C "$BASE" describe --tags --always 2>/dev/null || echo unknown)
export ZEROOS_ARCH=${ZEROOS_ARCH:-arm64}
# payload() can take programs the build made from here (Desktop's viewer).
export ZEROOS_BUILD=$BUILD

ids=("$@")
if [ ${#ids[@]} -eq 0 ]; then
    for pkg in "$ROOT"/apps/*/*.pkg; do ids+=("$(basename "$pkg" .pkg)"); done
fi
# Every module needs an app folder: nothing gets left out. A module an app
# here replaces (REPLACES=, a renamed app the pinned OS still lists) is left out.
if [ $# -eq 0 ]; then
    replaced=" $(sed -n 's/^REPLACES="\(.*\)".*/\1/p' "$ROOT"/apps/*/*.pkg | tr '\n' ' ') "
    for m in "$BUILD"/apps/*.so; do
        id=$(basename "$m" .so)
        [[ $replaced == *" $id "* ]] && continue
        [ -f "$ROOT/apps/$id/$id.pkg" ] || { echo "error: no apps/$id/$id.pkg for $m" >&2; exit 1; }
    done
fi

mkdir -p "$OUT"
rm -f -- "${OUT:?}"/zeroos-app-*.deb "${OUT:?}/zeroos-apps.index"
{
    echo "# ZeroOS apps. Read by zeroos-apps on the device."
    echo "# level abi=$ABI os=$OS"
    echo
    for id in "${ids[@]}"; do
        dir=$ROOT/apps/$id
        pkg=$dir/$id.pkg
        [ -f "$pkg" ] || { echo "error: no $pkg" >&2; exit 1; }
        [ -f "$BUILD/apps/$id.so" ] || { echo "error: no module $BUILD/apps/$id.so for $pkg" >&2; exit 1; }
        version=$(sed -n 's/^APP_VERSION="\([0-9][0-9.]*\)"$/\1/p' "$pkg")
        [ -n "$version" ] || { echo "error: $pkg needs APP_VERSION=\"1.2.3\"" >&2; exit 1; }
        export WORK=$ROOT/build/work/$id
        mkdir -p "$WORK"
        if [ -f "$dir/prepare.sh" ]; then bash "$dir/prepare.sh" >&2; fi
        # The level is part of the version: the same app built for a newer
        # ZeroOS is a newer package, and both can sit in one release.
        ZEROOS_FILES=$dir/files ZEROOS_APP_ABI=$ABI \
            "$BASE/image/apps/make-app-deb.sh" "$pkg" "$OUT" "$version-abi$ABI" "$BUILD/apps/$id.so"
    done
} > "$OUT/zeroos-apps.index.tmp"
mv -f "$OUT/zeroos-apps.index.tmp" "$OUT/zeroos-apps.index"
ls -l "$OUT" >&2
