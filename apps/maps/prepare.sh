#!/usr/bin/env bash
# Maps' map (README.md): New Jersey, built from Geofabrik's OpenStreetMap
# extract with tools/zmap_build.cpp, into $WORK/maps. Kept if it is already
# there. To build from files you already have:
#   ZS_MAP_PBF=new-jersey.osm.pbf ZS_MAP_POLY=new-jersey.poly
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$WORK/maps/new-jersey.zmap
[ -s "$OUT" ] && exit 0
URL=https://download.geofabrik.de/north-america/us

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
mkdir -p "$WORK/maps"
"$BUILDER" --name "New Jersey" ${POLY:+--poly "$POLY"} --imperial "$PBF" "$OUT.new" >&2
# A map of a whole state is tens of megabytes; much less means the data was bad.
size=$(stat -c %s "$OUT.new")
[ "$size" -gt 10000000 ] || { echo "error: the map came out at only $size bytes" >&2; exit 1; }
mv -f "$OUT.new" "$OUT"
[ -n "${ZS_MAP_PBF:-}" ] || rm -f "$WORK/new-jersey.osm.pbf"
