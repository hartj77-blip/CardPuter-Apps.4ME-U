#!/usr/bin/env bash
# Open every app in the headless build and save a picture of each.
#   tools/screenshots.sh <build-dir> <out-dir>
# The build compiles every app in (cmake -S . -B <dir> -DZS_BACKEND=headless).
# Fails if an app crashes.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=$(realpath "$1")/zeroshell
OUT=$(realpath -m "$2")
mkdir -p "$OUT"
DEMO=$(mktemp -d)
trap 'rm -rf -- "${DEMO:?}"' EXIT
# The app ids, from every app.cmake.
ids=$(sed -n 's/^zs_app(\([a-z0-9-]*\) .*/\1/p' "$ROOT"/apps/*/app.cmake)
steps="shot:home.ppm"
for id in $ids; do steps+=",open:$id,wait:600,shot:$id.ppm"; done
export ZS_GAMES_CORES="$(dirname "$BIN")/test-cores"
export ZEROOS_ZCLAW_FAKE="Four."
(cd "$OUT" && HOME="$DEMO" TZ=UTC "$BIN" --script "$steps,home")
python3 "$ROOT/base/shell/tools/ppm2png.py" "$OUT"
for id in $ids; do
    [ -s "$OUT/$id.png" ] || { echo "FAIL no screen for $id"; exit 1; }
done
echo "ok   every app opens: $(echo $ids)"
