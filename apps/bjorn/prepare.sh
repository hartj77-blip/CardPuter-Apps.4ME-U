#!/usr/bin/env bash
# Vendor the Bjorn source (infinition/Bjorn, MIT) into $WORK/bjorn at a pinned
# commit, and make it run headless on ZeroOS:
#   - drop in the epd_mock driver (files/epd_mock.py) so no e-paper HAT, and no
#     RPi.GPIO / spidev, is needed;
#   - default epd_type to epd_mock so the mock loads at startup.
# Bjorn's Display still renders and writes web/screen.png, so the web UI and the
# Bjorn face work; only the physical panel is stubbed. Kept if already fetched.
#
# Bump COMMIT deliberately and re-check that it still starts (journalctl -u
# bjorn): Bjorn's startup imports its action modules, a few of which need Python
# packages that are not in Debian (see README).
set -euo pipefail

REPO="https://github.com/infinition/Bjorn"
COMMIT="61a4758e5ee3d37d43c10eb690340626139912c0"
DEST="$WORK/bjorn"

[ -f "$DEST/Bjorn.py" ] && exit 0
rm -rf "$DEST" "$DEST.tmp"

git clone --filter=blob:none "$REPO" "$DEST.tmp"
git -C "$DEST.tmp" checkout -q "$COMMIT"
rm -rf "$DEST.tmp/.git" "$DEST.tmp/.github"

# Headless by default: load the mock e-paper driver instead of the Waveshare
# one. $F is this app's files/ folder (make-app-deb.sh sets it); fall back to
# the script's own dir when run by hand.
src_files="${F:-$(cd "$(dirname "$0")" && pwd)/files}"
install -D -m 0644 "$src_files/epd_mock.py" "$DEST.tmp/resources/waveshare_epd/epd_mock.py"
sed -i 's/"epd_type": *"epd2in13_V4"/"epd_type": "epd_mock"/' "$DEST.tmp/shared.py"
grep -q '"epd_type": "epd_mock"' "$DEST.tmp/shared.py" \
    || { echo "error: could not set epd_type default to epd_mock in shared.py" >&2; exit 1; }

mv "$DEST.tmp" "$DEST"
