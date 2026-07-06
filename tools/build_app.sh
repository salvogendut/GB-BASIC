#!/usr/bin/env bash
# Build a GB-BASIC app (multiple C files) into a raw #4000 GEOBENCH app image —
# the same output format as geobench's tools/build_capp.sh, generalized so an app
# directory may hold more than one .c file (build_capp.sh compiles only main.c).
#
#   tools/build_app.sh <app_dir> <out.RAW>
#   env: GEOBENCH   path to the geobench checkout (default ../geobench)
#        DATA_LOC   app data split (default 0x6200): code #4000..DATA_LOC,
#                   data DATA_LOC..#7FFF
#        APPDEFS    extra defines for EVERY unit (e.g. -DGB_MSX2 — must reach
#                   libgb C too: gb.h derives GB_COLS/GB_LINES from it, #287)
#        DOC=1      link gbui_stub + gbdoc (the File-menu document framework)
#        BUILD_DIR  intermediates dir (default build/<app>[-msx])
set -euo pipefail
cd "$(dirname "$0")/.."

APP="${1:?usage: build_app.sh <app_dir> <out.RAW>}"
OUT="${2:?usage: build_app.sh <app_dir> <out.RAW>}"
GEOBENCH="${GEOBENCH:-../geobench}"
GB="$GEOBENCH/lib/gb"
DATA_LOC="${DATA_LOC:-0x6200}"
DOC_FLAG="${DOC:-0}"

SDCC="${SDCC:-sdcc}"
BIN="$(dirname "$(command -v "$SDCC")")"    # sdasz80 / makebin sit beside sdcc
SDAS="$BIN/sdasz80"
MAKEBIN="$BIN/makebin"

suffix=""
case "${APPDEFS:-}" in *GB_MSX2*) suffix="-msx" ;; esac
work="${BUILD_DIR:-build/$(basename "$APP")$suffix}"
mkdir -p "$work" "$(dirname "$OUT")"

"$SDAS" -o "$work/crt0.rel"  "$GB/crt0.s"
"$SDAS" -o "$work/gblib.rel" "$GB/gblib.s"

# --fomit-frame-pointer: frame on IY, not IX — the kernel/fs code uses IX as
# scratch and firmware calls preserve IY, so a kernel call can't wreck the app's
# frame pointer (SDCC's epilogue is `ld sp,<fp>`). Must be uniform across ALL
# units, and APPDEFS must reach every unit too (geobench #287).
CFLAGS="-mz80 --fomit-frame-pointer ${APPDEFS:-} -I $GB -I $APP"

APP_RELS=""
for src in "$APP"/*.s; do                       # app-local asm (e.g. the float engine)
    [ -e "$src" ] || continue
    rel="$work/$(basename "${src%.s}").rel"
    "$SDAS" -o "$rel" "$src"
    APP_RELS="$APP_RELS $rel"
done
for src in "$APP"/*.c; do
    rel="$work/$(basename "${src%.c}").rel"
    "$SDCC" $CFLAGS -c "$src" -o "$rel"
    if [ "$(basename "$src")" = "main.c" ]; then
        APP_RELS="$rel $APP_RELS"           # main first (cosmetic; crt0 leads the link)
    else
        APP_RELS="$APP_RELS $rel"
    fi
done

"$SDCC" $CFLAGS -c "$GB/gbwin.c" -o "$work/gbwin.rel"
DLG_REL="$work/gbwin.rel"
if [ "$DOC_FLAG" = "1" ]; then
    "$SDCC" $CFLAGS -c "$GB/gbui_stub.c" -o "$work/gbui_stub.rel"
    "$SDCC" $CFLAGS -c "$GB/gbdoc.c"     -o "$work/gbdoc.rel"
    DLG_REL="$DLG_REL $work/gbui_stub.rel $work/gbdoc.rel"
fi

# crt0 FIRST so _start lands at #4000; z80.lib (float/long support) is linked
# automatically by the sdcc driver.
"$SDCC" -mz80 --no-std-crt0 --code-loc 0x4000 --data-loc "$DATA_LOC" \
    "$work/crt0.rel" $APP_RELS $DLG_REL "$work/gblib.rel" -o "$work/app.ihx"

# STABILITY GUARD (from geobench build_capp.sh): the whole LOADED IMAGE
# (_CODE + _GSINIT/_GSFINAL/_INITIALIZER) must end below data-loc, and data+bss
# must end below the kernel at #8000 — otherwise gsinit zeroes its own code as
# it runs -> instant reboot. Also print the area map: we watch sizes constantly.
python3 - "$work/app.map" "$APP" "$DATA_LOC" <<'PY'
import sys, re
mapf, app, dloc = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
area = {}
for line in open(mapf):
# _HOME matters here, unlike geobench's check: z80.lib float/long routines land
# in _HOME (after _GSFINAL), and it is part of the LOADED IMAGE — missing it let
# the data area overlap the float library (gsinit zeroed lib code).
    m = re.match(r'^(_CODE|_HOME|_DATA|_BSS|_INITIALIZED|_GSINIT|_GSFINAL|_INITIALIZER)\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})', line)
    if m and m.group(1) not in area:
        area[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
for a in sorted(area, key=lambda k: area[k][0]):
    s, sz = area[a]
    print('  %-13s %04X..%04X  %5d bytes' % (a, s, s + sz, sz))
LOAD = ('_CODE', '_HOME', '_GSINIT', '_GSFINAL', '_INITIALIZER')
img_end = max((area[a][0] + area[a][1]) for a in LOAD if a in area)
top = max((s + sz) for s, sz in area.values()) if area else 0
print('  image end %04X (data-loc %04X, %+d spare) | data end %04X (%+d spare below 8000)'
      % (img_end, dloc, dloc - img_end, top, 0x8000 - top))
errs = []
if img_end > dloc:  errs.append('loaded image ends 0x%04X > data-loc 0x%04X (gsinit/data overlap)' % (img_end, dloc))
if top > 0x8000:    errs.append('data/bss ends 0x%04X > kernel 0x8000' % top)
if errs:
    sys.stderr.write('FIT ERROR (%s): %s - shrink it or adjust DATA_LOC\n' % (app, '; '.join(errs)))
    sys.exit(1)
PY

"$MAKEBIN" -p "$work/app.ihx" "$work/app.bin"
tail -c +16385 "$work/app.bin" > "$OUT"     # strip the low 16K (image is org #4000)
echo "Built $OUT ($(stat -c%s "$OUT") bytes) from $APP"
