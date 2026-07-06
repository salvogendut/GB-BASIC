#!/usr/bin/env bash
# run_msx.sh - headless MSX2 smoke test: boot a GEOBENCH MSX hard-disk image in
# openMSX with a graphics-demo BASRUN auto-launched by the screensaver, and
# screenshot the result. Verifies the MSX build + the Screen-6 pixel packing.
#
# Needs: openMSX (Flatpak org.openmsx.openMSX or `openmsx` on PATH), a GEOBENCH
# MSX image (default ../geobench/QA/GBMSX.IMG), and `mcopy`/`mtype` (mtools).
set -euo pipefail
cd "$(dirname "$0")/.."
GEOBENCH="${GEOBENCH:-../geobench}"
SRCIMG="${1:-$GEOBENCH/QA/GBMSX.IMG}"
IMG=build/msx/boot.img
POFF=$((32 * 512))                 # FAT16 partition offset (build_msx_img.sh)
SHOT="${SHOT:-build/msx/run.png}"
AT="${AT:-175}"

[ -s "$SRCIMG" ] || { echo "ERROR: $SRCIMG not found (build the geobench MSX image)" >&2; exit 1; }
[ -f build/msx/BASRUN2.BIN ] || { echo "run 'make raws-msx' first" >&2; exit 1; }

# graphics-demo BASRUN (BUILTIN) as the screensaver, engine in root + \GBENCH.
APPDEFS=-DGB_MSX2 NOGBWIN=1 DATA_LOC=0x7F40 bash tools/build_app.sh apps/basrun build/msx/BASRUNBI.RAW >/dev/null
mkdir -p build/msx
cp "$SRCIMG" "$IMG"
cp build/msx/BASRUNBI.RAW build/msx/BASRUN.SAV
mcopy -o -i "$IMG@@$POFF" build/msx/BASRUN.SAV  ::BASRUN.SAV
mcopy -o -i "$IMG@@$POFF" build/msx/BASRUN.SAV  ::GBENCH/BASRUN.SAV
mcopy -o -i "$IMG@@$POFF" build/msx/BASRUN2.BIN ::BASRUN2.BIN
mcopy -o -i "$IMG@@$POFF" build/msx/BASRUN2.BIN ::GBENCH/BASRUN2.BIN
printf 'FONT=DEFAULT\r\nICONS=REFINED\r\nCURSOR=DEFAULT\r\nVIEW=DEFAULT\r\nBACKDROP=SOLID\r\nWALLPAPER=LOGO\r\nSAVER=BASRUN\r\nSAVERTIME=1\r\n' > build/msx/_cfg
mcopy -o -i "$IMG@@$POFF" build/msx/_cfg ::GEOBENCH.CFG

if command -v openmsx >/dev/null 2>&1; then OMSX=(openmsx)
else OMSX=(flatpak run --command=openmsx org.openmsx.openMSX); fi

TCL=build/msx/_run.tcl
printf 'set throttle off\nafter time %s { catch { screenshot -raw %s/%s } ; exit }\n' \
    "$AT" "$PWD" "$SHOT" > "$TCL"
SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}" \
    "${OMSX[@]}" -machine Philips_NMS_8250 -ext SunriseIDE_Nextor -ext ram512k \
    -hda "$IMG" -script "$TCL" >/dev/null 2>&1 || true
[ -f "$SHOT" ] && echo "Shot: $SHOT" || { echo "NO SCREENSHOT" >&2; exit 1; }
