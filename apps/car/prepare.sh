#!/usr/bin/env bash
# iPod mode's two outside parts (README.md), built into $WORK/ipod and kept
# if they are already there:
#   - the kernel driver that makes the USB port look like an iPod
#     (github.com/oandrew/ipod-gadget), for the one kernel ZeroOS runs;
#   - the program that speaks the iPod's protocol to the car
#     (github.com/oandrew/ipod), with ipod/buttons.patch, which makes it say
#     which button the car pressed.
# Both are fetched at one commit each, named below.
#
# Needs: git, curl, dpkg-deb, make, gcc-14, patch and go. On a PC (not
# arm64) also gcc-14-aarch64-linux-gnu, qemu-user-static and
# libc6-arm64-cross: the kernel's build helpers are arm64 programs.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$WORK/ipod

# The kernel of the ZeroOS image. The image holds that package (M5's board
# drivers are built for it too), so a device stays on it; on any other
# kernel the Car app says iPod mode isn't built for it.
KERNEL_BASE=6.18.39+rpt
KERNEL=$KERNEL_BASE-rpi-v8
KERNEL_DEB=6.18.39-1+rpt1
POOL=http://archive.raspberrypi.com/debian/pool/main/l/linux
GADGET_URL=https://github.com/oandrew/ipod-gadget
GADGET_COMMIT=ece6b7b0b29dacb7be0cf9f6436fc390892969e8
CLIENT_URL=https://github.com/oandrew/ipod
CLIENT_COMMIT=3762132cc9ad94c5301dddb9365a923350e0020f

# Built again when this script or the patch changes (the commits are named here).
stamp=$(cat "$0" "$HERE/ipod/buttons.patch" | sha256sum | cut -d' ' -f1)
if [ "$(cat "$OUT/stamp" 2>/dev/null)" = "$stamp" ] && [ -x "$OUT/ipod" ] && [ -f "$OUT/modules/$KERNEL/g_ipod_gadget.ko" ]; then
    exit 0
fi
for tool in git curl dpkg-deb make patch go; do
    command -v "$tool" >/dev/null || { echo "error: Car's prepare.sh needs $tool" >&2; exit 1; }
done
rm -rf "$OUT"
mkdir -p "$OUT/dl" "$OUT/sysroot" "$OUT/modules/$KERNEL"

# fetch URL COMMIT DIR: that one commit, whatever the branches say now.
fetch() {
    git init -q "$3"
    git -C "$3" fetch -q --depth 1 "$1" "$2"
    git -C "$3" -c advice.detachedHead=false checkout -q FETCH_HEAD
    [ "$(git -C "$3" rev-parse HEAD)" = "$2" ] || { echo "error: $1 isn't at $2" >&2; exit 1; }
}

# --- The kernel driver. The kernel's headers and build helpers come from
# Raspberry Pi's packages, unpacked here rather than installed.
while read -r sum deb; do
    curl -fsSL --retry 3 -o "$OUT/dl/$deb" "$POOL/$deb"
    echo "$sum  $OUT/dl/$deb" | sha256sum -c --quiet -
    dpkg-deb -x "$OUT/dl/$deb" "$OUT/sysroot"
done <<LIST
e5e5619d9d048dd43ec5db90a09aeb563596a7b1c4d2bbeeae3f69cec1301813 linux-headers-${KERNEL}_${KERNEL_DEB}_arm64.deb
5904d2c6a3e8fafe75ce3d53d86220b8057290d86cb95a96ad787a6900f4b344 linux-headers-${KERNEL_BASE}-common-rpi_${KERNEL_DEB}_all.deb
e3deb75129c67f0eafe5cec64eccc7ec4601890c25788487209fec1a3f605dad linux-kbuild-${KERNEL_BASE}_${KERNEL_DEB}_arm64.deb
LIST
KDIR=$OUT/sysroot/usr/src/linux-headers-$KERNEL
# Its Makefile names the shared headers by where the package would install them.
sed -i "s#/usr/src/#$OUT/sysroot/usr/src/#g" "$KDIR/Makefile"
cross=()
if [ "$(uname -m)" != aarch64 ]; then
    for tool in aarch64-linux-gnu-gcc-14 qemu-aarch64-static; do
        command -v "$tool" >/dev/null || { echo "error: building Car's iPod driver on a PC needs $tool" >&2; exit 1; }
    done
    # The helpers (fixdep, modpost...) are arm64 programs: each is run through qemu.
    find "$OUT/sysroot/usr/lib/linux-kbuild-$KERNEL_BASE" -type f | while read -r f; do
        [ "$(od -An -tx1 -N4 "$f" | tr -d ' ')$(od -An -tx1 -j18 -N2 "$f" | tr -d ' ')" = 7f454c46b700 ] || continue
        mv "$f" "$f.arm64"
        printf '#!/bin/sh\nexec qemu-aarch64-static -L /usr/aarch64-linux-gnu -0 "$0" "$0.arm64" "$@"\n' > "$f"
        chmod 0755 "$f"
    done
    cross=(ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-)
fi
fetch "$GADGET_URL" "$GADGET_COMMIT" "$OUT/gadget-src"
make -s -C "$KDIR" M="$OUT/gadget-src/gadget" "${cross[@]}" modules >&2
for m in g_ipod_audio g_ipod_hid g_ipod_gadget; do
    ko=$OUT/gadget-src/gadget/$m.ko
    # Built for another kernel it would not load.
    grep -aq "vermagic=$KERNEL " "$ko" || { echo "error: $m.ko isn't for $KERNEL" >&2; exit 1; }
    install -m 0644 "$ko" "$OUT/modules/$KERNEL/$m.ko"
done
install -m 0644 "$OUT/gadget-src/LICENSE" "$OUT/LICENSE.ipod-gadget"

# --- The client: Go, built for arm64 from any machine.
fetch "$CLIENT_URL" "$CLIENT_COMMIT" "$OUT/client-src"
patch -s -d "$OUT/client-src" -p1 < "$HERE/ipod/buttons.patch"
(cd "$OUT/client-src" &&
    GOOS=linux GOARCH=arm64 CGO_ENABLED=0 GOTOOLCHAIN=local GOFLAGS=-modcacherw \
    GOMODCACHE="$OUT/go/mod" GOCACHE="$OUT/go/cache" \
    go build -trimpath -ldflags="-s -w" -o "$OUT/ipod" ./cmd/ipod) >&2
install -m 0644 "$OUT/client-src/LICENSE" "$OUT/LICENSE.ipod"
# The licences of the Go code linked into it: each module's, and Go's own.
mkdir -p "$OUT/licenses"
(cd "$OUT/client-src" && GOFLAGS=-modcacherw GOMODCACHE="$OUT/go/mod" GOTOOLCHAIN=local \
    go list -deps -f '{{with .Module}}{{.Path}} {{.Dir}}{{end}}' ./cmd/ipod | sort -u) |
    while read -r module dir; do
        [ "$module" = github.com/oandrew/ipod ] && continue
        for f in "$dir"/LICENSE* "$dir"/COPYING*; do
            if [ -f "$f" ]; then install -m 0644 "$f" "$OUT/licenses/$(echo "$module" | tr / _).$(basename "$f")"; fi
        done
    done
# Go's is in its folder, or where Debian's package puts it.
for f in "$(go env GOROOT)/LICENSE" /usr/share/doc/golang-*-go/copyright /usr/share/doc/golang-go/copyright; do
    if [ -f "$f" ]; then install -m 0644 "$f" "$OUT/licenses/go.LICENSE"; break; fi
done
[ -f "$OUT/licenses/go.LICENSE" ] || { echo "error: Go's licence wasn't found" >&2; exit 1; }
rm -rf "$OUT/go" "$OUT/dl" "$OUT/sysroot"
echo "$stamp" > "$OUT/stamp"
