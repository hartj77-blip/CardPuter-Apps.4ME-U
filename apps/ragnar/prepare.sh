#!/usr/bin/env bash
# Vendor the Ragnar source into $WORK/ragnar at a pinned commit, pruned to what
# the headless web service needs. Ragnar (PierreGode/Ragnar, MIT) is a fork of
# Bjorn (infinition/Bjorn, MIT); its headlessRagnar.py entrypoint drops the
# e-paper display so it runs web-only on any Linux box. Kept if already fetched.
#
# Bump COMMIT deliberately: Ragnar moves fast, and a new commit can change its
# dependencies or the files this package installs. Check the app still starts
# (journalctl -u ragnar) after moving the pin.
set -euo pipefail

REPO="https://github.com/PierreGode/Ragnar"
COMMIT="313ee7e34a1d3b554a3e7e04561c9d9297a17022"
DEST="$WORK/ragnar"

[ -f "$DEST/headlessRagnar.py" ] && exit 0
rm -rf "$DEST" "$DEST.tmp"

git clone --filter=blob:none "$REPO" "$DEST.tmp"
git -C "$DEST.tmp" checkout -q "$COMMIT"
rm -rf "$DEST.tmp/.git" "$DEST.tmp/.github"

# Drop what the headless web service never loads: microcontroller firmware,
# demos, docker and lab kits, and the test suite. The Python modules stay
# (display.py included: it is never imported in headless mode, RAGNAR_HEADLESS,
# but some module may import it behind a guard).
for d in cyd_firmware espnow_bridge_firmware roomscan_firmware rusense_flasher \
         pager_bin demos docker lab tests; do
    rm -rf "$DEST.tmp/$d"
done

mv "$DEST.tmp" "$DEST"
