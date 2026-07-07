#!/usr/bin/env bash
# build_pcw_dsk.sh - pack the PCW deliverable dist/GBBASIC-PCW.DSK: a CF2
# CP/M data disk carrying the PCW builds of BASIC.APP, BASRUN.APP, the
# BASRUN2.BIN overlay and the example .BAS programs.
#
# Mount it in drive B alongside GEOBENCH's QA/PCW/GEOBENCH.DSK; open Disk B in
# the File Manager and run BASIC.APP. The PCW target is flat CP/M 2.2, so all
# files live in the root directory.
set -euo pipefail
cd "$(dirname "$0")/.."
GEOBENCH="${GEOBENCH:-../geobench}"
MKPCWDSK="$GEOBENCH/tools/mkpcwdsk.py"
DSK=dist/GBBASIC-PCW.DSK
mkdir -p dist

[ -r "$MKPCWDSK" ] || { echo "ERROR: $MKPCWDSK not found/readable" >&2; exit 1; }
for f in build/pcw/BASIC.RAW build/pcw/BASRUN.RAW build/pcw/BASRUN2.BIN; do
    [ -f "$f" ] || { echo "missing $f - run 'make raws-pcw' first" >&2; exit 1; }
done

# CR+LF for the .BAS examples.
mkdir -p build/expcw
for b in examples/*.BAS; do
    sed 's/$/\r/' "$b" > "build/expcw/$(basename "$b")"
done

rm -f "$DSK"
ADDS=(
    --add build/pcw/BASIC.RAW=BASIC.APP
    --add build/pcw/BASRUN.RAW=BASRUN.APP
    --add build/pcw/BASRUN2.BIN=BASRUN2.BIN
)
for b in build/expcw/*.BAS; do
    ADDS+=(--add "$b=$(basename "$b")")
done
python3 "$MKPCWDSK" "$DSK" "${ADDS[@]}"

echo "Built $DSK (CF2 CP/M data disk)."
