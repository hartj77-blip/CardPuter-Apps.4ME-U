#!/usr/bin/env bash
# Checks zeroos-usb-stick's modes (files/zeroos-usb-stick) with systemctl,
# modprobe, insmod and the kernel's gadget folder standing in as stubs: that
# it takes the mode the Car app named, sets up the right thing, says so, and
# takes it away again.
#   apps/car/tests/car-usb-test.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/bin" "$T/udc/fe980000.usb" "$T/run" "$T/var" "$T/live" "$T/ipod"
# systemctl: notes what it was asked; starting live sound makes its stick appear.
cat > "$T/bin/systemctl" <<STUB
#!/bin/sh
echo "systemctl \$*" >> "$T/calls"
case "\$*" in
  "start zeroos-car-live.service") [ -e "$T/live-fails" ] && exit 1; : > "$T/live/stick.img" ;;
  *is-active*) exit 3 ;;
esac
exit 0
STUB
for tool in modprobe insmod rmmod runuser; do
    printf '#!/bin/sh\necho "%s $*" >> "%s/calls"\n' "$tool" "$T" > "$T/bin/$tool"
done
printf '#!/bin/sh\nexit 0\n' > "$T/bin/lame"
cp "$T/bin/lame" "$T/bin/pw-record"
cp "$T/bin/lame" "$T/bin/aplay"
chmod +x "$T/bin/"*
export PATH="$T/bin:$PATH"
export ZEROOS_CAR_STATE="$T/run/state" ZEROOS_CAR_MODE_STATE="$T/run/mode" ZEROOS_CAR_MODE_FILE="$T/asked"
export ZEROOS_CAR_DIR="$T/var" ZEROOS_CAR_GADGET="$T/gadget" ZEROOS_CAR_UDC_DIR="$T/udc" ZEROOS_MUSIC="$T/Music"
export ZEROOS_CAR_LIVE_DIR="$T/live" ZEROOS_CAR_IPOD_DIR="$T/ipod" ZEROOS_CAR_KERNEL=1.2.3-test
S="$HERE/../files/zeroos-usb-stick"
fails=0
is() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', wanted '$3'"; fails=$((fails + 1)); fi; }
called() { if grep -qxF "$2" "$T/calls" 2>/dev/null; then echo "ok   $1"; else echo "FAIL $1: no '$2' in: $(tr '\n' ';' < "$T/calls" 2>/dev/null)"; fails=$((fails + 1)); fi; }
state() { cat "$T/run/state" 2>/dev/null || echo none; }
fresh() { rm -rf "$T/calls" "$T/gadget" "$T/run/"* "$T/live/"*; }

# USB live sound.
fresh; echo live > "$T/asked"
"$S" start
is "live: ready" "$(state)" ready
is "live: the mode is told" "$(cat "$T/run/mode")" live
called "live: its service is started" "systemctl start zeroos-car-live.service"
is "live: the car is offered its stick" "$(cat "$T/gadget/functions/mass_storage.0/lun.0/file")" "$T/live/stick.img"
is "live: read-only" "$(cat "$T/gadget/functions/mass_storage.0/lun.0/ro")" 1
is "live: on the device port" "$(cat "$T/gadget/UDC")" fe980000.usb
: > "$T/calls"
"$S" stop
called "live: stopping stops its service" "systemctl stop --no-block zeroos-car-live.service"
is "live: stopped, nothing is said" "$(state)$(cat "$T/run/mode" 2>/dev/null)" none

# Its service doesn't start: said, and nothing left offered.
fresh; : > "$T/live-fails"
if "$S" start 2>/dev/null; then is "live: a failed start fails" yes no; else is "live: a failed start fails" no no; fi
is "live: and says why" "$(state)" "error: live sound didn't start"
is "live: and offers nothing" "$(cat "$T/gadget/UDC" 2>/dev/null)" ""
rm -f "$T/live-fails"

# Its service stops by itself while the stick is offered: taken away, and said.
fresh
"$S" start
"$S" live-gone
is "live: gone by itself" "$(state)" "error: live sound stopped"
is "live: the stick is taken away" "$(cat "$T/gadget/UDC")" ""
# ... but not when we stopped it.
fresh
"$S" start; "$S" stop; "$S" live-gone
is "live: stopped by us, no complaint" "$(state)" none

# iPod mode: no driver for this kernel.
fresh; echo ipod > "$T/asked"
if "$S" start 2>/dev/null; then is "ipod: no driver fails" yes no; else is "ipod: no driver fails" no no; fi
is "ipod: and says so" "$(state)" "error: iPod mode isn't built for this kernel (1.2.3-test)"
is "ipod: the mode is told" "$(cat "$T/run/mode")" ipod

# Anything else in the file is USB stick mode; with no music that fails, as it always did.
fresh; printf 'ipod; reboot\n' > "$T/asked"
"$S" start 2>/dev/null || true
is "an odd word is the stick" "$(cat "$T/run/mode")" files
fresh; rm -f "$T/asked"
"$S" start 2>/dev/null || true
is "no word is the stick" "$(cat "$T/run/mode")" files
is "the stick with no music says so" "$(state)" "error: no Music folder"

# No device port (the switch is on Host).
fresh; echo live > "$T/asked"; rmdir "$T/udc/fe980000.usb"
"$S" start 2>/dev/null || true
is "no device port" "$(state)" "error: no USB device port: set the switch on the left to Slave"

[ "$fails" = 0 ] || exit 1
echo "car-usb-test: all passed"
