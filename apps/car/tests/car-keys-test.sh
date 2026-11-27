#!/usr/bin/env bash
# Checks zeroos-car-keys (iPod mode: the car's buttons, as its client
# reports them, become key presses) without a keyboard to press on: with
# --dry-run it prints the key codes instead.
#   apps/car/tests/car-keys-test.sh [build-dir]     (default: build/headless)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
BIN=$(realpath "${1:-$ROOT/build/headless}")/bin/zeroos-car-keys
[ -x "$BIN" ] || { echo "error: no $BIN: cmake -S . -B build/headless -DZS_BACKEND=headless && cmake --build build/headless" >&2; exit 1; }
# The client's log lines, and buttons among them. Key codes: next 163,
# play/pause 164, previous 165, play 200, pause 201 (linux/input-event-codes.h).
got=$(printf '%s\n' 'time="2026-01-01" level=info msg="serving"' 'button next' 'button playpause' \
    'button previous' 'button play' 'button pause' 'button eject' 'buttonnext' 'button next; reboot' |
    "$BIN" --dry-run 2>/dev/null | tr '\n' ' ')
want="163 164 165 200 201 "
if [ "$got" = "$want" ]; then echo "ok   the five buttons, and nothing else"; else echo "FAIL keys: got '$got', wanted '$want'"; exit 1; fi
log=$(printf 'hello\nbutton next\n' | "$BIN" --dry-run 2>&1 >/dev/null)
case $log in *hello*"the car says next"*) echo "ok   the client's log is passed on" ;; *) echo "FAIL the log: $log"; exit 1 ;; esac
echo "car-keys-test: all passed"
