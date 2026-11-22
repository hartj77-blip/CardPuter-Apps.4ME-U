#!/usr/bin/env bash
# IR Remote's code database (README.md), from Flipper-IRDB, into $WORK/irdb.
# Kept if it is already there.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
[ -f "$WORK/irdb/index.tsv" ] ||
    python3 -I "$HERE/irdb/build_irdb.py" --fetch --cache "$WORK/irdb-src" --out "$WORK/irdb"
