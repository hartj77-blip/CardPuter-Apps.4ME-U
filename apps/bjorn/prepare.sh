#!/usr/bin/env bash
# Vendor the Bjorn source (infinition/Bjorn, MIT) into $WORK/bjorn at a pinned
# commit, and make it run headless on ZeroOS:
#   - drop in the epd_mock driver (files/epd_mock.py), a no-hardware e-paper EPD;
#   - replace epd_helper.py (files/epd_helper.py) with one that falls back to
#     the mock when the real Waveshare driver can't import (no HAT, so no
#     RPi.GPIO / spidev / SPI device).
# The configured epd_type is left as the real model (epd2in13_V4): shared.py
# only sets screen_reversed / web_screen_reversed for known models, so forcing
# an "epd_mock" type would leave those unset and crash display.py. The loader
# uses the mock for the (absent) panel while the model name keeps those flags
# correct. Bjorn's Display still renders and writes web/screen.png, so the web
# UI and the Bjorn face work; only the physical panel is stubbed. Kept if
# already fetched.
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

# Headless: ship the mock EPD and the fallback loader. $F is this app's files/
# folder (make-app-deb.sh sets it); fall back to the script's own dir when run
# by hand.
src_files="${F:-$(cd "$(dirname "$0")" && pwd)/files}"
install -D -m 0644 "$src_files/epd_mock.py" "$DEST.tmp/resources/waveshare_epd/epd_mock.py"
install -D -m 0644 "$src_files/epd_helper.py" "$DEST.tmp/epd_helper.py"
grep -q 'headless mock' "$DEST.tmp/epd_helper.py" \
    || { echo "error: epd_helper.py fallback not installed" >&2; exit 1; }

# Python 3.13 (Debian trixie) removed cgi and telnetlib from the stdlib
# (PEP 594); Bjorn imports both (utils.py, the telnet actions). Ship vendored
# copies in the tree root so `import cgi` / `import telnetlib` resolve there.
install -D -m 0644 "$src_files/cgi.py" "$DEST.tmp/cgi.py"
install -D -m 0644 "$src_files/telnetlib.py" "$DEST.tmp/telnetlib.py"

mv "$DEST.tmp" "$DEST"
