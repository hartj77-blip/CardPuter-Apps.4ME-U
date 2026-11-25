#!/usr/bin/env bash
# Checks zeroos-car-wifi (Wi-Fi off while a phone's music plays, unless
# connected) with nmcli, busctl and bluetoothctl standing in as stubs.
#   apps/car/tests/car-wifi-test.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/bin" "$T/state"
cat > "$T/bin/nmcli" <<STUB
#!/bin/sh
case "\$*" in
  "-t radio wifi") cat "$T/radio" ;;
  "radio wifi on") echo enabled > "$T/radio"; echo on >> "$T/calls" ;;
  "radio wifi off") echo disabled > "$T/radio"; echo off >> "$T/calls" ;;
  "-t -f TYPE,STATE device") echo "wifi:\$(cat "$T/wifi")"; echo "loopback:connected (externally)" ;;
esac
STUB
cat > "$T/bin/busctl" <<STUB
#!/bin/sh
case "\$*" in
  *tree*) [ -s "$T/status" ] && printf '/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF\n/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player0\n' ;;
  *get-property*) echo "s \"\$(cat "$T/status")\"" ;;
esac
STUB
cat > "$T/bin/bluetoothctl" <<'STUB'
#!/bin/sh
printf 'Device AA:BB:CC:DD:EE:FF\n\tIcon: phone\n'
STUB
chmod +x "$T/bin/"*
export PATH="$T/bin:$PATH" ZEROOS_CAR_WIFI_STATE="$T/state" ZEROOS_CAR_WIFI_GRACE=3
W="$HERE/../files/zeroos-car-wifi"
fails=0
check() { if [ "$(cat "$T/radio")" = "$2" ]; then echo "ok   $1"; else echo "FAIL $1 (Wi-Fi $(cat "$T/radio"), wanted $2)"; fails=$((fails + 1)); fi; }
set_() { echo "$1" > "$T/radio"; echo "$2" > "$T/wifi"; echo "$3" > "$T/status"; : > "$T/calls"; rm -f "$T/state"/*; }
once() { "$W" once >/dev/null; }

set_ enabled disconnected playing; once
check "playing, Wi-Fi not connected: off" disabled
echo paused > "$T/status"; once; once
check "paused for a moment: still off" disabled
once
check "paused long enough: on again" enabled

set_ enabled connected playing; once
check "playing, Wi-Fi connected: left on" enabled

set_ enabled connecting playing; once
check "playing, Wi-Fi connecting: left on" enabled

set_ enabled disconnected paused; once
check "nothing playing: left on" enabled

set_ enabled disconnected playing; once
echo enabled > "$T/radio"; once; once
check "turned on by hand while playing: stays on" enabled
echo stopped > "$T/status"; once; once; once
check "music stopped after that: still on" enabled
[ ! -e "$T/state/wifi-keep" ] && echo "ok   keep forgotten when the music stops" || { echo "FAIL keep kept"; fails=$((fails + 1)); }

set_ disabled disconnected playing; once
check "Wi-Fi already off: not touched" disabled
[ ! -s "$T/calls" ] && echo "ok   ...and never turned on" || { echo "FAIL touched Wi-Fi that was off"; fails=$((fails + 1)); }

set_ enabled disconnected playing; once
: > "$T/status"; "$W" restore >/dev/null
check "restore (service stop, restart): on again" enabled

set_ enabled disconnected playing; echo "Icon: audio-card" > /dev/null
cat > "$T/bin/bluetoothctl" <<'STUB'
#!/bin/sh
printf 'Device AA:BB:CC:DD:EE:FF\n\tIcon: audio-card\n\tUUID: Audio Sink (0000110b-0000)\n'
STUB
once
check "a car's own player playing: not a phone, left on" enabled

[ "$fails" = 0 ] && echo "car-wifi-test: all passed" || exit 1
