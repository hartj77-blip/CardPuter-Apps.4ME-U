#!/usr/bin/env bash
# USB live sound for real, short of the USB cable: runs zeroos-car-live with
# a stand-in encoder, and reads the stick it serves the way a car would.
#   apps/car/tests/car-live-test.sh [build-dir]     (default: build/headless)
# Needs: root (it mounts; sudo is used if it asks no password), /dev/fuse,
# python3, dosfstools and mtools. Without root or /dev/fuse it says so and
# passes.
#
# What it checks:
#   - the stick is a sound FAT32 volume (fsck) with the three songs (mdir)
#   - a song starts with its name tag, then whole MP3 frames in order
#   - the first three seconds of sound come at once, the rest as fast as it
#     plays and no faster
#   - a read from the middle of another song starts on a frame
#   - the sound carries on, as silence, when the encoder dies, and the
#     encoder is started again
#   - stopping the program takes the stick away
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
BIN=$(realpath "${1:-$ROOT/build/headless}")/bin/zeroos-car-live
[ -x "$BIN" ] || { echo "error: no $BIN: cmake -S . -B build/headless -DZS_BACKEND=headless && cmake --build build/headless" >&2; exit 1; }
if [ "$(id -u)" != 0 ]; then
    if sudo -n true 2>/dev/null; then exec sudo -n env PATH="$PATH" "$0" "$@"; fi
    echo "skip car-live-test: needs root to mount"
    exit 0
fi
[ -c /dev/fuse ] || { echo "skip car-live-test: no /dev/fuse here"; exit 0; }
for tool in python3 fsck.vfat mdir; do
    command -v "$tool" >/dev/null || { echo "error: car-live-test needs $tool (apt install dosfstools mtools)" >&2; exit 1; }
done

T=$(mktemp -d)
PID=
cleanup() {
    [ -n "$PID" ] && kill "$PID" 2>/dev/null && wait "$PID" 2>/dev/null
    umount -l "$T/mnt" 2>/dev/null || true
    rm -rf -- "${T:?}"
}
trap cleanup EXIT

# The stand-in encoder: 320 kbit/s frames with nothing in them but their
# number, as fast as the program takes them. Its process id goes to a file,
# so the test can kill it.
cat > "$T/encoder" <<EOF
#!/usr/bin/env python3
import os, sys
open("$T/encoder.pid", "w").write(str(os.getpid()))
n = int(open("$T/encoder.runs").read()) * 1000000 if os.path.exists("$T/encoder.runs") else 0
open("$T/encoder.runs", "w").write(str(n // 1000000 + 1))
out = sys.stdout.buffer
while True:
    out.write(b"\xff\xfb\xe0\x44" + bytes(36) + n.to_bytes(4, "big") + bytes(1000))
    out.flush()
    n += 1
EOF
chmod +x "$T/encoder"

"$BIN" --no-keys "$T/mnt" -- "$T/encoder" 2> "$T/log" &
PID=$!
for _ in $(seq 50); do [ -f "$T/mnt/stick.img" ] && break; sleep 0.1; done
IMG=$T/mnt/stick.img
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
[ -f "$IMG" ] || { bad "the stick appears"; cat "$T/log"; exit 1; }
sleep 4   # a few seconds of sound behind it, as when a car is plugged in

# The volume. fsck wants the partition by itself: everything before the
# songs is copied out, and the songs' place left empty.
SONG1=$(( (2048 + 32 + 2 * 769 + 32) * 512 ))   # where the first song starts
SIZE=$(stat -c %s "$IMG")
head -c "$SONG1" "$IMG" > "$T/start"
truncate -s "$SIZE" "$T/start"
dd if="$T/start" of="$T/part" bs=1M skip=1 conv=sparse status=none
if fsck.vfat -n "$T/part" > "$T/fsck" 2>&1; then ok "fsck finds nothing wrong"; else bad "fsck: $(tail -3 "$T/fsck")"; fi
list=$(MTOOLS_SKIP_CHECK=1 mdir -i "$IMG@@1048576" :: 2>&1)
if [ "$(echo "$list" | grep -c '^LIVE[123]  *MP3  *536870912 ')" = 3 ]; then ok "three songs of 512 MiB"; else bad "the songs: $list"; fi

# The songs, read like a car: straight from the stick, 16 KiB at a time.
python3 - "$IMG" "$SONG1" "$T" <<'EOF' || fails=$((fails + 1))
import os, signal, sys, time
img, song1, tmp = sys.argv[1], int(sys.argv[2]), sys.argv[3]
FRAME, FILE = 1044, 512 * 1024 * 1024
fails = 0
def check(what, good, more=""):
    global fails
    print(("ok   " if good else "FAIL ") + what + ("" if good else "  " + more))
    fails += 0 if good else 1
def frames(data):
    """The frame numbers in data, which must be whole frames back to back."""
    out = []
    for at in range(0, len(data) - FRAME + 1, FRAME):
        if data[at:at + 4] != b"\xff\xfb\xe0\x44":
            return None
        out.append(int.from_bytes(data[at + 40:at + 44], "big"))
    return out
def in_order(numbers):
    return numbers is not None and all(b == a + 1 for a, b in zip(numbers, numbers[1:]))

fd = os.open(img, os.O_RDONLY)
def read(at, size):
    got = b""
    while len(got) < size:
        got += os.pread(fd, min(16384, size - len(got)), at + len(got))
    return got

# One song from its start: 300,000 bytes is 7.5 s of sound, of which 3 s
# (the backlog) is there already.
t = time.time()
first = read(song1, 120000)
quick = time.time() - t
rest = read(song1 + 120000, 180000 + 512)
took = time.time() - t
check("a song starts with its name", first[:3] == b"ID3" and b"Live sound" in first[:64])
numbers = frames((first + rest)[512:])
check("then whole frames in order", in_order(numbers), str(numbers and numbers[:5]))
check("the first three seconds come at once", quick < 1.0, "%.1f s" % quick)
check("the rest as fast as it plays", 3.0 < took < 9.0, "%.1f s" % took)

# Another song, from the middle.
mid = read(song1 + FILE + 100 * 1048576 + 5, 8 * FRAME)
check("the middle of another song starts on a frame", in_order(frames(mid)))

# The encoder dies: silence carries the song on, and it is started again.
os.kill(int(open(tmp + "/encoder.pid").read()), signal.SIGKILL)
t = time.time()
after = read(song1 + 2 * FILE, 280000)
took = time.time() - t
ok = all(after[at:at + 4] == b"\xff\xfb\xe0\x44" for at in range(512, len(after) - FRAME, FRAME))
check("the song carries on when the encoder dies", ok and took < 9.0, "%.1f s" % took)
again = [n for n in (int.from_bytes(after[at + 40:at + 44], "big") for at in range(512, len(after) - FRAME, FRAME)) if n >= 1000000]
check("and the encoder is started again", len(again) > 10, "%d frames from the new one" % len(again))
sys.exit(1 if fails else 0)
EOF

kill "$PID" && wait "$PID" 2>/dev/null || true
PID=
if [ -e "$IMG" ]; then bad "stopping takes the stick away"; else ok "stopping takes the stick away"; fi
grep -q "the encoder stopped" "$T/log" || bad "the log says the encoder stopped"
[ "$fails" = 0 ] || { echo "--- the program's log"; cat "$T/log"; exit 1; }
echo "car-live-test: all passed"
