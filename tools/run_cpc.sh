#!/usr/bin/env bash
# run_cpc.sh - launch a GB-BASIC app inside GEOBENCH on the 1984 emulator,
# headless, and capture a screenshot. Click-free: the app is packed onto a
# drive-B disk as SPIKE.SAV and a custom boot floppy (SAVER=B:SPIKE,
# SAVERTIME=1) auto-launches it via the desktop's screensaver idle timer
# ~3000 frames after the desktop is up.
#
#   tools/run_cpc.sh <app.RAW> <shot-frame> <out.ppm> [extra 1984 args...]
#
# Notes discovered the hard way (see docs/VERIFYING.md):
#   - 1984 --joy-script silently caps the queue at 128 steps.
#   - The FM double-click-to-open never triggers under scripted joystick fire;
#     single clicks and the top-bar menus work. Hence the saver route.
#   - A .BAS file argument can be given to the app by also packing it onto the
#     B disk and having the app load it (BASRUN loads its launch file when
#     started from the FM; as a saver it starts file-less -> BUILTIN_TEST).
set -euo pipefail
cd "$(dirname "$0")/.."
export SDL_VIDEODRIVER=dummy   # headless: no emulator window on the user's screen

RAW="${1:?usage: run_cpc.sh <app.RAW> <shot-frame> <out.ppm>}"
FRAME="${2:?shot frame (desktop ~2300, saver fires ~5400; use 6000+)}"
OUT="${3:?output .ppm}"
shift 3

GEOBENCH="${GEOBENCH:-../geobench}"
EMU="${EMU:-../1984/1984}"

# --- boot floppy with the saver config -----------------------------------------
# Built from a PINNED geobench worktree (build/geo-clean, at the HEAD this
# project was verified against) so the user's in-progress geobench edits can't
# break the harness. Seed it once with: git -C ../geobench worktree add --detach
# build/geo-clean HEAD && cp -r ../geobench/build/* build/geo-clean/build/
# Force a rebuild with FORCE_BOOTDSK=1 (e.g. after moving the pin).
GEOK="${GEOK:-build/geo-clean}"
if [ ! -f build/bootsav.dsk ] || [ "${FORCE_BOOTDSK:-0}" = "1" ]; then
    printf 'FONT=DEFAULT\r\nICONS=REFINED\r\nCURSOR=DEFAULT\r\nVIEW=DEFAULT\r\nBACKDROP=SOLID\r\nWALLPAPER=LOGO\r\nSAVER=B:SPIKE\r\nSAVERTIME=1\r\n' \
        > "$GEOK/build/GEOBENCH.CFG"
    ( cd "$GEOK" && rm -f build/gbkern.dsk \
        && rasm kernel/gbkern.asm -eo -DSTORAGE_ALBIREO=1 >/dev/null 2>&1 \
        && rasm kernel/pack_apps.asm    >/dev/null 2>&1 \
        && rasm kernel/pack_apps2.asm   >/dev/null 2>&1 \
        && rasm kernel/pack_apps3.asm   >/dev/null 2>&1 \
        && rasm kernel/pack_modules.asm >/dev/null 2>&1 )   # FLOPPYSV.MOD (disk save)
    cp "$GEOK/build/gbkern.dsk" build/bootsav.dsk
    echo "Rebuilt build/bootsav.dsk (SAVER=B:SPIKE boot floppy, pinned worktree)"
fi

# --- drive-B disk: the app as SPIKE.SAV (+SPIKE.APP), plus any EXTRA files ----
# BASRUN needs its float-engine overlay alongside; ship it whenever it exists.
if [ -f build/BASRUN2.BIN ]; then
    EXTRA_FILES="${EXTRA_FILES:-} build/BASRUN2.BIN"
fi
mkdir -p build
cp "$RAW" build/_TEST.RAW
{
    echo '        org #4000'
    echo 's0      incbin "_TEST.RAW"'
    echo 's0e'
    echo '        save "SPIKE.APP",s0,s0e-s0,DSK,"build/spike.dsk"'
    echo '        save "SPIKE.SAV",s0,s0e-s0,DSK,"build/spike.dsk"'
    n=1
    for f in ${EXTRA_FILES:-}; do       # extra files (e.g. .BAS programs), 8.3 upper
        base=$(basename "$f")
        cp "$f" "build/_X$n.BIN"
        echo "x$n      incbin \"_X$n.BIN\""
        echo "x${n}e"
        echo "        save \"$base\",x$n,x${n}e-x$n,DSK,\"build/spike.dsk\""
        n=$((n+1))
    done
} > build/_pack.asm
rm -f build/spike.dsk
rasm build/_pack.asm -ob /dev/null >/dev/null 2>&1

# drive A gets the engine overlay too: a saver-launched BASRUN loads it via the
# boot-drive lookup (the browse-drive fallback only applies to FM-launched apps)
cp build/bootsav.dsk build/boota.dsk
if [ -f build/BASRUN2.BIN ]; then
    cp build/BASRUN2.BIN build/_ENG.BIN
    {
        echo '        org #4000'
        echo 'e0      incbin "_ENG.BIN"'
        echo 'e0e'
        echo '        save "BASRUN2.BIN",e0,e0e-e0,DSK,"build/boota.dsk"'
    } > build/_epack.asm
    rasm build/_epack.asm -ob /dev/null >/dev/null 2>&1
fi

"$EMU" --config=/dev/null --6128 --memory=512 \
    --disk-a=build/boota.dsk --disk-b=build/spike.dsk --autostart=GBKERN \
    --screenshot-at="$FRAME:$OUT" --exit-after=$((FRAME+10)) "$@" 2>/dev/null || true
[ -f "$OUT" ] && echo "Shot: $OUT" || { echo "NO SCREENSHOT PRODUCED" >&2; exit 1; }
