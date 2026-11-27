#!/usr/bin/env bash
# The Jellyfin app against a stand-in server (fake_jellyfin.py), end to end:
# the headless ZeroShell with the device's own hardware layer signs in over
# real HTTP, and this checks what the app asked the server for and what it
# drew.
#   apps/jellyfin/tests/server-test.sh [build-dir]     (default: build/headless)
# Needs: curl, python3 with Pillow (the stand-in draws the covers).
#
# What it checks:
#   - the sign-in token goes in the Authorization header of every request and
#     never in an address
#   - Home asks for the libraries, Continue Watching, Next Up and what is new
#   - covers are fetched as JPEG at the size of a card, drawn, kept on disk and
#     not fetched twice
#   - a long library is fetched a page at a time, to the end
#   - playing a track opens the player with its cover; the queue picks
#     another track; leaving the app and coming back, Y finds the player again
#   - a search asks once for each kind of thing
#   - a favourite reaches the server
#   - signing out forgets the token, the covers and the searches
#   - a token the server no longer takes signs the app out
#   - a server from before 10.9 (lists under /Users/<id>/) works too
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
BIN=$(realpath "${1:-$ROOT/build/headless}")/zeroshell
[ -x "$BIN" ] || { echo "error: no $BIN: cmake -S . -B build/headless -DZS_BACKEND=headless && cmake --build build/headless" >&2; exit 1; }
T=$(mktemp -d)
SERVER=
cleanup() {
    # Under set -e: a kill of what has already gone must not end this early.
    if [ -n "$SERVER" ]; then kill "$SERVER" 2>/dev/null || true; fi
    if [ -s "$T/mpv.pids" ]; then kill $(cat "$T/mpv.pids") 2>/dev/null || true; fi
    rm -rf -- "${T:?}"
}
trap cleanup EXIT
mkdir -p "$T/shots" "$T/bin"
# A stand-in mpv: music "plays" (ZeroShell's player starts mpv for a stream)
# and what it was asked to play is written down.
cat > "$T/bin/mpv" <<STUB
#!/bin/sh
echo \$\$ >> "$T/mpv.pids"
printf '%s\n' "\$*" >> "$T/mpv.log"
exec sleep 60
STUB
chmod +x "$T/bin/mpv"
H=$T/home   # the Cardputer's home folder: its settings and its covers
CONF=$H/.config/zeroos/settings.conf
CACHE=$H/.cache/zeroos/jellyfin

serve() {   # [--old]: start the stand-in (again), on a port of its own
    if [ -n "$SERVER" ]; then kill "$SERVER" 2>/dev/null || true; wait "$SERVER" 2>/dev/null || true; fi
    rm -f "$T/port"
    python3 "$HERE/fake_jellyfin.py" --port-file "$T/port" --log "$T/log" "$@" 2>/dev/null &
    SERVER=$!
    for _ in $(seq 100); do [ -s "$T/port" ] && break; sleep 0.1; done
    [ -s "$T/port" ] || { echo "FAIL the stand-in server didn't start (python3 with Pillow?)"; exit 1; }
    URL=http://127.0.0.1:$(cat "$T/port")
}

run() {   # steps: one visit to the app; what the server saw is in $T/log
    : > "$T/log"
    mkdir -p "$H"
    if ! (cd "$T/shots" && PATH="$T/bin:$PATH" HOME="$H" TZ=UTC "$BIN" --real-hal --script "$1") > "$T/out" 2>&1; then
        echo "FAIL zeroshell: $(tail -5 "$T/out")"
        exit 1
    fi
    if grep -q "AddressSanitizer\|runtime error" "$T/out"; then
        echo "FAIL a memory error:"; grep -A12 "AddressSanitizer\|runtime error" "$T/out" | head -40
        exit 1
    fi
}

check() {   # python: `log` is the requests of the last visit; shot(name) counts a picture's cover pixels
    python3 - "$T" "$@" <<PY || exit 1
import json, os, sys
T = sys.argv[1]
log = [json.loads(l) for l in open(os.path.join(T, "log")) if l.strip()]
def shot(name):
    """How many pixels of the picture are the magenta the stand-in puts on every cover."""
    data = open(os.path.join(T, "shots", name), "rb").read()
    parts = data.split(None, 4)   # P6 w h 255 pixels
    px = parts[4]
    return sum(1 for i in range(0, len(px) - 2, 3) if px[i] > 200 and px[i + 1] < 70 and px[i + 2] > 200)
def paths(prefix, method="GET"):
    return [r for r in log if r["path"].startswith(prefix) and r["method"] == method]
def ok(cond, what):
    if not cond:
        print("FAIL", what)
        for r in log[-12:]:
            print("    ", r["status"], r["method"], r["path"], r["query"][:90])
        sys.exit(1)
$(cat)
PY
}

SIGN_IN="open:jellyfin,wait:300,enter,wait:300,type:URL,enter,wait:300,type:john,enter,wait:300,type:secret,enter"
# After signing out the server and the user are still in their boxes.
SIGN_AGAIN="open:jellyfin,wait:300,enter,wait:300,enter,wait:300,enter,wait:300,type:secret,enter"

# ---- A first visit: sign in, look round, search, sign out ---------------------
serve
run "${SIGN_IN/URL/$URL},live:4000,shot:home.ppm\
,enter,live:2500,shot:albums.ppm,enter,live:2500,shot:album.ppm\
,enter,live:2000,shot:player.ppm,key:q,live:600,shot:queue.ppm,down,enter,live:1500,shot:player2.ppm,key:v,live:1200\
,home,live:300,open:jellyfin,live:2500,key:y,live:1000,shot:player-again.ppm,esc,live:400\
,right,right,right,right,enter,live:5000,shot:archive.ppm,esc,wait:300\
,key:f,live:600,type:love,live:3500,shot:search.ppm,esc,wait:200,esc,wait:300\
,ctrl:o,wait:400,enter,live:800,shot:signed-out.ppm"
check <<'PY'
sign = paths("/Users/AuthenticateByName", "POST")
ok(len(sign) == 1 and sign[0]["status"] == 200, "one sign-in")
rest = [r for r in log if r["path"] not in ("/Users/AuthenticateByName", "/System/Info/Public")]
ok(rest and all(r["token"] for r in rest), "every request carries the token in its Authorization header")
ok(not any(r["token_in_url"] for r in log), "the token is never in an address")
ok(all(r["status"] in (200, 204, 404) for r in rest), "no request refused")
# The app is opened twice on this visit (it is left and opened again).
for p in ("/UserViews", "/UserItems/Resume", "/Shows/NextUp"):
    ok(len(paths(p)) == 2, "Home asks once for " + p + " each time the app opens")
ok(len(paths("/Items/Latest")) >= 4, "Home asks what is new in each library")
ok(not paths("/Users/", "GET"), "a current server isn't asked for the old paths")

pics = [r for r in log if "/Images/" in r["path"]]
ok(len(pics) >= 12, "covers are asked for")
ok(all("fillWidth=" in r["query"] and "fillHeight=" in r["query"] and "format=Jpg" in r["query"] for r in pics),
   "covers are asked for at a card's size, as JPEG")
ok(len({(r["path"], r["query"]) for r in pics}) == len(pics), "no cover is asked for twice")
ok(shot("home.ppm") > 150, "Home shows the covers")
ok(shot("albums.ppm") > 40, "the music library shows its albums' covers")
ok(shot("album.ppm") > 40, "an album's page shows its cover")
ok(shot("signed-out.ppm") == 0, "signed out: no covers on the screen")

starts = sorted(int(q.split("startIndex=")[1].split("&")[0]) for q in
                (r["query"] for r in paths("/Items") if r["path"] == "/Items" and "startIndex=" in r["query"]
                 and "includeItemTypes=Movie&" in r["query"]))
ok(starts == [0, 100, 200], f"a library of 230 comes in three pages (asked from rows {starts})")

found = [r for r in log if "searchTerm=love&" in r["query"]]
kinds = sorted(r["query"].split("includeItemTypes=")[1].split("&")[0] for r in found if r["path"] == "/Items")
ok(kinds == sorted(["Movie", "Series", "Episode", "Video,MusicVideo", "BoxSet", "Playlist", "MusicAlbum", "Audio",
                    "AudioBook"]), f"a search asks once for each kind of thing ({kinds})")
ok(len([r for r in found if r["path"] == "/Artists"]) == 1, "and for artists from their own list")
ok(not any("searchTerm=l&" in r["query"] or "searchTerm=lo&" in r["query"] for r in log),
   "not for every letter typed on the way")
ok(shot("search.ppm") > 40, "search results show their covers")

fav = paths("/UserFavoriteItems/", "POST")
ok(len(fav) == 1 and fav[0]["status"] == 200, "a favourite reaches the server")

ok(shot("player.ppm") > 100, "playing a track opens the player, with the album's cover")
ok(shot("player2.ppm") > 100, "a track picked in the queue plays in the player")
ok(shot("player-again.ppm") > 100, "after leaving the app, Y opens the player with its cover")
streams = [l for l in open(os.path.join(T, "mpv.log")).read().splitlines() if "/Audio/" in l]
ids = [l.split("/Audio/")[1].split("/")[0] for l in streams]
ok(len(ids) == 2 and ids[0] != ids[1], f"the first track, then the one picked in the queue ({len(ids)} streams)")
ok(all("--include=" in l and "Token" not in l for l in streams), "the token goes to mpv in a file, not on its command line")
PY
grep -q '^jellyfin.token=$' "$CONF" || { echo "FAIL signing out left the token in $CONF"; exit 1; }
grep -q '^jellyfin.searches=$' "$CONF" || { echo "FAIL signing out left the searches in $CONF"; exit 1; }
[ -z "$(ls -A "$CACHE/art")" ] || { echo "FAIL signing out left covers in $CACHE/art"; exit 1; }
[ -z "$(ls -A "$CACHE/tmp")" ] || { echo "FAIL files left in $CACHE/tmp: $(ls "$CACHE/tmp")"; exit 1; }
[ "$(stat -c %a "$CACHE")" = 700 ] || { echo "FAIL $CACHE can be read by others"; exit 1; }
echo "ok   signs in, browses, searches and signs out against a server"

# ---- Covers are kept: a second look at Home fetches none again ----------------
run "$SIGN_AGAIN,live:4000,shot:home.ppm"
[ -n "$(ls -A "$CACHE/art")" ] || { echo "FAIL no covers kept in $CACHE/art"; exit 1; }
run "open:jellyfin,live:3500,shot:home2.ppm"
check <<'PY'
ok(len(paths("/UserViews")) == 1, "still signed in on the next visit")
ok(not [r for r in log if "/Images/" in r["path"]], "covers come from the Cardputer's own copy the second time")
ok(shot("home2.ppm") > 150, "and are on the screen")
PY
echo "ok   keeps covers between visits"

# ---- A token the server no longer takes -----------------------------------
sed -i 's/^jellyfin.token=.*/jellyfin.token=00000000000000000000000000000000/' "$CONF"
run "open:jellyfin,live:3000,shot:refused.ppm"
check <<'PY'
ok(any(r["status"] == 401 for r in log), "the server refused the old token")
ok(shot("refused.ppm") == 0, "nothing of the library shows")
PY
grep -q '^jellyfin.token=$' "$CONF" || { echo "FAIL a refused token was kept"; exit 1; }
echo "ok   signs out when the server refuses the token"

# ---- A server from before 10.9 --------------------------------------------
serve --old
H=$T/home-old
run "${SIGN_IN/URL/$URL},live:5000,shot:old.ppm"
check <<'PY'
ok(paths("/UserViews") and paths("/UserViews")[0]["status"] == 404, "the current path was tried first")
user = [r for r in paths("/Users/") if r["status"] == 200]
ok(any(r["path"].endswith("/Views") for r in user), "then the libraries under /Users/<id>/Views")
ok(any(r["path"].endswith("/Items/Resume") for r in user), "Continue Watching under /Users/<id>/Items/Resume")
ok(any(r["path"].endswith("/Items/Latest") for r in user), "what is new under /Users/<id>/Items/Latest")
ok(shot("old.ppm") > 150, "Home shows the covers")
PY
echo "ok   works with a server from before 10.9"
