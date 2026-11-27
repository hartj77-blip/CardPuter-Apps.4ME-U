#!/usr/bin/env bash
# Checks irdb/build_irdb.py on the sample (irdb/sample): converted Pronto
# remotes get a kind and the main set's brand spelling, their raw codes merge
# with the same codes in the universal lists, and the AV kinds and buttons
# are there.
#   apps/ir/tests/irdb-test.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
python3 -I "$HERE/../irdb/build_irdb.py" --src "$HERE/../irdb/sample" --out "$T/db" > "$T/log"
fail() { echo "FAIL: $*" >&2; exit 1; }

grep -q "2 converted" "$T/log" || fail "two Pronto remotes: $(cat "$T/log")"
# Filed under the kind it is listed as, so "# src:" lines name kind and brand.
grep -qP '^Projectors\tEpson\tEMP-Raw\tProjectors/Epson/EMP-Raw.ir\t2$' "$T/db/index.tsv" ||
    fail "Epson (brand: projectors) in Projectors"
grep -qP '^TVs\tSony\tKV-27FS\tTVs/Sony/KV-27FS.ir\t2$' "$T/db/index.tsv" || fail "Sony KV- (model: TV) in TVs"
[ -f "$T/db/TVs/Sony/KV-27FS.ir" ] || fail "the file is where index.tsv says"

for k in TVs Projectors Audio Lights; do [ -f "$T/db/universal/$k.ir" ] || fail "universal/$k.ir"; done
# The Pronto Sony power is raw SIRC 01/15: the same code as Sony_Bravia's, so one entry.
n=$(grep -A4 '^name: Power$' "$T/db/universal/TVs.ir" | grep -c '^command: 15 00 00 00') || true
[ "$n" = 1 ] || fail "Sony power once in TVs Power, not $n times"
grep -q '^type: raw' "$T/db/universal/TVs.ir" && fail "no raw codes in TVs (all decode)"
# The Epson raw Source is NECext 83 55 / 8C 73: listed under Projectors Input.
grep -B5 '^command: 8C 73 00 00' "$T/db/universal/Projectors.ir" | grep -q '^name: Input$' ||
    fail "Epson Source under Projectors Input"

python3 -I - "$HERE/../irdb" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1])
import build_irdb as b
cases = [
    (("Draper", "Sonata", {"up", "down", "stop"}), "Projector_Screens"),
    (("Sony", "VPL-VW1000ES", {"power"}), "Projectors"),
    (("Sony", "BDP-S580", {"power"}), "Blu-Ray"),
    (("Sony", "KDL-46EX720", {"power"}), "TVs"),
    (("Denon", "AVR-3600_RECEIVER", {"power"}), "Audio_and_Video_Receivers"),
    (("Polycom", "HDX-7000", {"power"}), "Videoconferencing"),
    (("Extron", "SYSTEM_5-CR", {"power"}), "Switchers_and_Scalers"),
    (("Acme", "Thing", {"keystone", "lamp", "blank", "power"}), "Projectors"),
    (("Acme", "Thing", {"eject", "title", "subtitle", "angle"}), "DVD_Players"),
    (("Acme", "Thing", {"tuner", "fm", "am", "surround"}), "Audio_and_Video_Receivers"),
    (("Acme", "Thing", {"power"}), "Miscellaneous"),
]
for args, want in cases:
    got = b.classify(*args)
    assert got == want, (args, got, want)
assert b.refile("Projectors", "Da-Lite", "Da-Lite_ProjectorScreen") == "Projector_Screens"
assert b.refile("Miscellaneous", "Brand_Unknown", "HDMI_Switch") == "Switchers_and_Scalers"
assert b.refile("Miscellaneous", "Extron", "Extron_IR903") == "Switchers_and_Scalers"
assert b.refile("Touchscreen_Displays", "SMART_Technologies", "SMART_Board_MX") == "Touchscreen_Displays"
assert b.refile("TVs", "LG", "LG_TV") == "TVs"
# decode(): clean frames only.
nec = [9000, 4500] + sum(([560, 1690] if (0x6F905583 >> i) & 1 else [560, 560] for i in range(32)), []) + [560]
d = b.decode({"name": "P", "type": "raw", "data": nec + [40000, 9000, 2250, 560]})
assert (d["protocol"], d["address"], d["command"]) == ("NECext", 0x5583, 0x6F90), d
assert b.decode({"name": "P", "type": "raw", "data": nec[:-3] + [560]}) is None
assert b.decode({"name": "P", "type": "parsed", "protocol": "NEC", "address": 1, "command": 2}) is None
EOF
echo "irdb-test: ok"
