#!/usr/bin/env bash
# The key of Meshtastic's openSUSE beta repository, into $WORK, checked
# against the fingerprint pinned here. Kept if it is already there.
set -euo pipefail
FPR=426AA6B0285C2096B70D9FC2528423A469A77D9A   # network OBS Project, expires 2027-08-26
[ -s "$WORK/meshtastic.asc" ] && exit 0
curl -fsSL --retry 3 -o "$WORK/meshtastic.asc.tmp" \
    https://download.opensuse.org/repositories/network:/Meshtastic:/beta/Debian_13/Release.key
got=$(gpg --show-keys --with-colons "$WORK/meshtastic.asc.tmp" | awk -F: '$1 == "fpr" {print $10; exit}')
[ "$got" = "$FPR" ] || { echo "error: Meshtastic key is $got, expected $FPR" >&2; exit 1; }
mv -f "$WORK/meshtastic.asc.tmp" "$WORK/meshtastic.asc"
