#!/usr/bin/env bash
# Maps' data (README.md), into $WORK/maps, from Geofabrik's OpenStreetMap
# extract of New Jersey:
#   new-jersey.zmap    the map, drawn by the app       (tools/zmap_build.cpp)
#   new-jersey.search  "Go to"'s index of places and streets
#   new-jersey.addr    the house numbers, for "Go to"
#   new-jersey.navit   the roads, for Navit to plan routes on (Navit's maptool)
# Kept if they are already there. To build from files you already have:
#   ZS_MAP_PBF=new-jersey.osm.pbf ZS_MAP_POLY=new-jersey.poly
# Needs zlib (zlib1g-dev) and maptool.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$WORK/maps
[ -s "$OUT/new-jersey.zmap" ] && [ -s "$OUT/new-jersey.search" ] && [ -s "$OUT/new-jersey.addr" ] && [ -s "$OUT/new-jersey.navit" ] && exit 0
URL=https://download.geofabrik.de/north-america/us
command -v maptool >/dev/null || { echo "error: Maps needs Navit's maptool to build its routing map: apt install maptool" >&2; exit 1; }

# The builder comes with the build (app.cmake); failing that, compile it here.
BUILDER=${ZEROOS_BUILD:-}/bin/zmap-build
if [ ! -x "$BUILDER" ]; then
    BUILDER=$WORK/zmap-build
    g++ -std=c++17 -O2 -o "$BUILDER" "$HERE/tools/zmap_build.cpp" -lz ||
        { echo "error: can't build zmap-build (it needs zlib: apt install zlib1g-dev)" >&2; exit 1; }
fi

PBF=${ZS_MAP_PBF:-}
POLY=${ZS_MAP_POLY:-}
if [ -z "$PBF" ]; then
    PBF=$WORK/new-jersey.osm.pbf
    POLY=$WORK/new-jersey.poly
    curl -fsSL --retry 3 -o "$PBF" "$URL/new-jersey-latest.osm.pbf"
    curl -fsSL --retry 3 -o "$POLY" "$URL/new-jersey.poly"
    # Geofabrik's checksum: catches a download cut short.
    want=$(curl -fsSL --retry 3 "$URL/new-jersey-latest.osm.pbf.md5" | cut -d' ' -f1)
    have=$(md5sum "$PBF" | cut -d' ' -f1)
    [ -n "$want" ] && [ "$want" = "$have" ] || { echo "error: $PBF doesn't match Geofabrik's checksum" >&2; exit 1; }
fi
mkdir -p "$OUT"
"$BUILDER" --name "New Jersey" ${POLY:+--poly "$POLY"} --imperial \
    --search "$OUT/new-jersey.search.new" --addresses "$OUT/new-jersey.addr.new" --roads "$WORK/new-jersey.roads.osm" "$PBF" "$OUT/new-jersey.zmap.new" >&2
maptool -i "$WORK/new-jersey.roads.osm" "$OUT/new-jersey.navit.new" >"$WORK/maptool.log" 2>&1 ||
    { tail -20 "$WORK/maptool.log" >&2; echo "error: maptool failed" >&2; exit 1; }
rm -f "$WORK/new-jersey.roads.osm"
# A whole state is tens of megabytes; much less means the data was bad.
for f in zmap:10000000 search:1000000 addr:1000000 navit:5000000; do
    size=$(stat -c %s "$OUT/new-jersey.${f%%:*}.new")
    [ "$size" -gt "${f##*:}" ] || { echo "error: new-jersey.${f%%:*} came out at only $size bytes" >&2; exit 1; }
done
for f in zmap search addr navit; do mv -f "$OUT/new-jersey.$f.new" "$OUT/new-jersey.$f"; done
[ -n "${ZS_MAP_PBF:-}" ] || rm -f "$WORK/new-jersey.osm.pbf"
