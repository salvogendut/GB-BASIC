#!/usr/bin/env bash
# build_msx_dsk.sh - pack the MSX2 deliverable dist/GBBASIC-MSX.DSK: a 720K
# FAT12 DATA disk carrying the MSX builds of BASIC.APP, BASRUN.APP, the
# BASRUN2.BIN overlay and the example .BAS programs.
#
# GEOBENCH on MSX resolves an app in the boot drive's \GBENCH first, then the
# current directory (lib/msx/fs.asm) - so the apps go in BOTH ::/GBENCH and the
# root, covering however the disk is mounted. Mount it in openMSX as a second
# disk (-diskb) alongside the GEOBENCH image; open it in the File Manager.
set -euo pipefail
cd "$(dirname "$0")/.."
DSK=dist/GBBASIC-MSX.DSK
mkdir -p dist

for f in build/msx/BASIC.RAW build/msx/BASRUN.RAW build/msx/BASRUN2.BIN; do
    [ -f "$f" ] || { echo "missing $f - run 'make raws-msx' first" >&2; exit 1; }
done
for t in mkfs.fat mcopy mmd; do
    command -v "$t" >/dev/null || { echo "ERROR: missing '$t' (dosfstools/mtools)" >&2; exit 1; }
done

# CR+LF for the .BAS examples.
mkdir -p build/exmsx
for b in examples/*.BAS; do
    sed 's/$/\r/' "$b" > "build/exmsx/$(basename "$b")"
done

rm -f "$DSK"
# 720K FAT12, standard MSX geometry (matches geobench tools/build_msx_floppy.sh).
mkfs.fat -F12 -S512 -s2 -R1 -f2 -r112 -h0 -n GBBASIC -C "$DSK" 720 >/dev/null
export MTOOLS_SKIP_CHECK=1

# .APP + overlay live in \GBENCH (the system dir the launcher looks in) AND at
# the root (the current-dir fallback).
mmd -i "$DSK" ::GBENCH
for pair in "build/msx/BASIC.RAW:BASIC.APP" \
            "build/msx/BASRUN.RAW:BASRUN.APP" \
            "build/msx/BASRUN2.BIN:BASRUN2.BIN"; do
    src="${pair%%:*}"; dst="${pair##*:}"
    mcopy -i "$DSK" "$src" "::GBENCH/$dst"
    mcopy -i "$DSK" "$src" "::$dst"
done
mcopy -i "$DSK" build/exmsx/*.BAS ::/

echo "Built $DSK (720K FAT12):"
mdir -i "$DSK" :: | sed 's/^/  /'
