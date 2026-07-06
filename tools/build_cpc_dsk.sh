#!/usr/bin/env bash
# build_cpc_dsk.sh - pack the CPC deliverable dist/GBBASIC.DSK: a non-bootable
# drive-B DATA disk (the COMPANION.DSK model) carrying BASIC.APP, BASRUN.APP,
# the BASRUN2.BIN float+graphics overlay, and the example .BAS programs.
#
# Mount it in drive B alongside GEOBENCH in drive A; open Disk B in the File
# Manager and double-click BASIC.APP. RASM's `save ...,DSK` adds a 128-byte
# AMSDOS header to each file, which the kernel FS strips on load.
set -euo pipefail
cd "$(dirname "$0")/.."
RASM="${RASM:-rasm}"
DSK=dist/GBBASIC.DSK
mkdir -p dist

for f in build/BASIC.RAW build/BASRUN.RAW build/BASRUN2.BIN; do
    [ -f "$f" ] || { echo "missing $f - run 'make raws' first" >&2; exit 1; }
done

# CR+LF for every .BAS the CPC will read (the examples ship LF in git).
mkdir -p build/ex
for b in examples/*.BAS; do
    sed 's/$/\r/' "$b" > "build/ex/$(basename "$b")"
done

rm -f "$DSK"
{
    echo '        org #4000'
    echo 'a0      incbin "../build/BASIC.RAW"'
    echo 'a0e'
    echo "        save \"BASIC.APP\",a0,a0e-a0,DSK,\"$DSK\""
    echo 'a1      incbin "../build/BASRUN.RAW"'
    echo 'a1e'
    echo "        save \"BASRUN.APP\",a1,a1e-a1,DSK,\"$DSK\""
    echo 'a2      incbin "../build/BASRUN2.BIN"'
    echo 'a2e'
    echo "        save \"BASRUN2.BIN\",a2,a2e-a2,DSK,\"$DSK\""
    n=0
    for b in build/ex/*.BAS; do
        base=$(basename "$b")
        echo "e$n     incbin \"../$b\""
        echo "e${n}e"
        echo "        save \"$base\",e$n,e${n}e-e$n,DSK,\"$DSK\""
        n=$((n + 1))
    done
} > build/pack_cpc.asm

"$RASM" build/pack_cpc.asm -ob /dev/null >/dev/null
echo "Built $DSK:"
python3 - "$DSK" <<'PY'
import sys
# crude AMSDOS catalogue read (track 0, sectors 0xC1..0xC4 at 0x0000 in a .dsk
# is emulator-specific; just report the file count RASM logged is easier) - so
# instead list the payload sizes we packed.
print("  (mount in drive B; open Disk B in the File Manager)")
PY
ls -l dist/GBBASIC.DSK
