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
# RF Tools: its radio daemon and helpers from this build, with a simulated radio.
export PATH="$(dirname "$BIN")/bin:$PATH" ZRFD_BIN="$(dirname "$BIN")/bin/zrfd"
export ZRFD_SOCKET="$DEMO/zrfd.sock" ZRF_SOURCE=sim ZRF_AUDIO=none
steps="shot:home.ppm"
# Apps that hold several tools (apps/kit/suite.h): each tool too, picked by
# its number in the app's menu (Net Tools 1-7, RF Tools 1-9).
declare -A tools=([net]=7 [rf]=9)
shots=""
for id in $ids; do
    steps+=",open:$id,wait:600,shot:$id.ppm"
    shots+=" $id"
    for ((n = 1; n <= ${tools[$id]:-0}; n++)); do
        steps+=",open:$id,key:$n,wait:600,shot:$id-$n.ppm"
        shots+=" $id-$n"
    done
done
export ZS_GAMES_CORES="$(dirname "$BIN")/test-cores"
export ZEROOS_AI_FAKE="Four."
(cd "$OUT" && HOME="$DEMO" TZ=UTC "$BIN" --script "$steps,home")
python3 "$ROOT/base/shell/tools/ppm2png.py" "$OUT"
for id in $shots; do
    [ -s "$OUT/$id.png" ] || { echo "FAIL no screen for $id"; exit 1; }
done
echo "ok   every app opens: $(echo $ids)"
