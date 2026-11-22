#!/usr/bin/env bash
# The key of Tailscale's Debian repository, into $WORK. Kept if it is there.
set -euo pipefail
[ -s "$WORK/tailscale.gpg" ] ||
    curl -fsSL --retry 3 -o "$WORK/tailscale.gpg" https://pkgs.tailscale.com/stable/debian/trixie.noarmor.gpg
