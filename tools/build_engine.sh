#!/usr/bin/env bash
# Build the BASRUN float-engine overlay (fac.s) as BASRUN2.BIN: standalone
# assembly with _CODE at 0x2200 and _DATA at 0x2F80 (see basrun.h LR_* map).
# BASRUN.APP gb_fs_loads this file into low RAM at startup.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-build/BASRUN2.BIN}"
SDCC="${SDCC:-sdcc}"
BIN="$(dirname "$(command -v "$SDCC")")"
work=build/engine
MSX2="${MSX2:-0}"
PCW="${PCW:-0}"
mkdir -p "$work" "$(dirname "$OUT")"
printf 'MSX2 = %s\nPCW = %s\n' "$MSX2" "$PCW" > "$work/plat.inc"
"$BIN/sdasz80" -o "$work/fac.rel" apps/basrun/fac.s
"$BIN/sdasz80" -I"$work" -o "$work/gfx.rel" apps/basrun/gfx.s
"$BIN/sdldz80" -n -m -i "$work/fac.ihx" -b _CODE=0x2200 -b _DATA=0x2F80 "$work/fac.rel" "$work/gfx.rel"
"$BIN/makebin" -p "$work/fac.ihx" "$work/fac.bin"
SIZE=$(stat -c%s "$work/fac.bin")
CODE_END=$SIZE                       # makebin -p ends at the last code byte
if [ "$CODE_END" -gt $((0x2F80)) ]; then
    echo "ENGINE TOO BIG: code ends at $CODE_END (> 0x2F80 data base)" >&2
    exit 1
fi
tail -c +$((0x2200 + 1)) "$work/fac.bin" > "$OUT"
echo "Built $OUT ($(stat -c%s "$OUT") bytes, code ends $(printf 0x%X "$CODE_END"))"
