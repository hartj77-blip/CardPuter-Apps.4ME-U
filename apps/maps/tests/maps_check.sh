#!/usr/bin/env bash
# Build the made-up town's map (make-town.py) with zmap-build, then run the
# tests on it: the map and renderer, Navit's answers and the search, and,
# where Navit is installed, a real route.
#   tests/maps_check.sh <zmap-build> <maps_test> <nav_test> <navit_test>
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$(realpath "$1")
TEST=$(realpath "$2")
NAV_TEST=$(realpath "$3")
NAVIT_TEST=$(realpath "$4")
TMP=$(mktemp -d)
trap 'rm -rf -- "${TMP:?}"' EXIT
"$BUILD" --name Zeroville --poly "$HERE/town.poly" --imperial --date 2026-01-01 \
    --search "$TMP/town.search" --roads "$TMP/town.roads.osm" \
    "$HERE/town.osm.pbf" "$TMP/town.zmap" 2>"$TMP/log" || { cat "$TMP/log"; echo "FAIL zmap-build"; exit 1; }
grep -q '^sea: 1 stretches of coast, 2 areas and islands' "$TMP/log" || { cat "$TMP/log"; echo "FAIL the sea wasn't built"; exit 1; }
# The roads a car may use, for Navit: no railway, river or driveway; the motorway and service road, yes.
grep -q 'wrote .*town.roads.osm: [0-9]* nodes, 11 roads' "$TMP/log" || { cat "$TMP/log"; echo "FAIL the roads for Navit"; exit 1; }
grep -q '<tag k="maxspeed" v="30 mph"/>' "$TMP/town.roads.osm" || { echo "FAIL the roads keep their tags"; exit 1; }
if grep -q 'railway\|waterway\|driveway' "$TMP/town.roads.osm"; then echo "FAIL only roads go to Navit"; exit 1; fi
# Without an outline the region is the box around the data.
"$BUILD" --name Box "$HERE/town.osm.pbf" "$TMP/box.zmap" 2>"$TMP/log" || { cat "$TMP/log"; echo "FAIL zmap-build without --poly"; exit 1; }
# What isn't an extract is refused, with nothing left behind.
if "$BUILD" --name Bad "$HERE/town.poly" "$TMP/bad.zmap" 2>/dev/null; then echo "FAIL a .poly was read as a .pbf"; exit 1; fi
[ ! -e "$TMP/bad.zmap" ] || { echo "FAIL a failed build left a map behind"; exit 1; }
"$TEST" "$TMP/town.zmap"
"$NAV_TEST" "$TMP/town.search"

# A real route, if Navit and what it needs are here.
for p in navit maptool Xvfb dbus-daemon dbus-send; do
    command -v "$p" >/dev/null || { echo "skip maps: a route by Navit (no $p here)"; exit 0; }
done
maptool -i "$TMP/town.roads.osm" "$TMP/town.navit" >"$TMP/maptool.log" 2>&1 || { tail "$TMP/maptool.log"; echo "FAIL maptool"; exit 1; }
XDG_RUNTIME_DIR=$TMP "$NAVIT_TEST" "$TMP/town.navit"
# Nothing of Navit's is left running, and its folder is gone.
sleep 0.5
if pgrep -f "navit -c $TMP/" >/dev/null; then echo "FAIL Navit was left running"; exit 1; fi
if ls -d "$TMP"/zeroos-maps-* >/dev/null 2>&1; then echo "FAIL Navit's folder was left behind"; exit 1; fi
