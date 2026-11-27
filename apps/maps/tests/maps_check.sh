#!/usr/bin/env bash
# Build the made-up town's map (make-town.py) with zmap-build, then run the
# reader and renderer tests on it.
#   tests/maps_check.sh <zmap-build> <maps_test>
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$(realpath "$1")
TEST=$(realpath "$2")
TMP=$(mktemp -d)
trap 'rm -rf -- "${TMP:?}"' EXIT
"$BUILD" --name Zeroville --poly "$HERE/town.poly" --imperial --date 2026-01-01 \
    "$HERE/town.osm.pbf" "$TMP/town.zmap" 2>"$TMP/log" || { cat "$TMP/log"; echo "FAIL zmap-build"; exit 1; }
grep -q '^sea: 1 stretches of coast, 2 areas and islands' "$TMP/log" || { cat "$TMP/log"; echo "FAIL the sea wasn't built"; exit 1; }
# Without an outline the region is the box around the data.
"$BUILD" --name Box "$HERE/town.osm.pbf" "$TMP/box.zmap" 2>"$TMP/log" || { cat "$TMP/log"; echo "FAIL zmap-build without --poly"; exit 1; }
# What isn't an extract is refused, with nothing left behind.
if "$BUILD" --name Bad "$HERE/town.poly" "$TMP/bad.zmap" 2>/dev/null; then echo "FAIL a .poly was read as a .pbf"; exit 1; fi
[ ! -e "$TMP/bad.zmap" ] || { echo "FAIL a failed build left a map behind"; exit 1; }
"$TEST" "$TMP/town.zmap"
