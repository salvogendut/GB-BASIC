# GB-BASIC — a GW-BASIC-style BASIC for GEOBENCH (CPC + MSX2 + PCW).
# Deliverables: dist/GBBASIC.DSK (CPC drive-B data disk),
# dist/GBBASIC-MSX.DSK, and dist/GBBASIC-PCW.DSK.
GEOBENCH ?= ../geobench
export GEOBENCH

.PHONY: all cpc msx pcw spike raws raws-msx raws-pcw dsk-cpc dsk-msx dsk-pcw qa-cpc qa-msx clean

all: cpc msx pcw

# --- CPC ---------------------------------------------------------------------
raws:
	DOC=1 DATA_LOC=0x6BF0 bash tools/build_app.sh apps/basic  build/BASIC.RAW
	bash tools/build_engine.sh
	APPDEFS=-DBUILTIN_OFF NOGBWIN=1 DATA_LOC=0x7F40 bash tools/build_app.sh apps/basrun build/BASRUN.RAW

dsk-cpc: raws
	bash tools/build_cpc_dsk.sh

cpc: dsk-cpc

# --- MSX ---------------------------------------------------------------------
raws-msx:
	APPDEFS=-DGB_MSX2 DOC=1 DATA_LOC=0x6BF0 bash tools/build_app.sh apps/basic  build/msx/BASIC.RAW
	MSX2=1 bash tools/build_engine.sh build/msx/BASRUN2.BIN
	APPDEFS="-DGB_MSX2 -DBUILTIN_OFF" NOGBWIN=1 DATA_LOC=0x7F40 bash tools/build_app.sh apps/basrun build/msx/BASRUN.RAW

dsk-msx: raws-msx
	bash tools/build_msx_dsk.sh

msx: dsk-msx

# --- PCW ---------------------------------------------------------------------
raws-pcw:
	APPDEFS=-DGB_PCW DOC=1 DATA_LOC=0x6BF0 bash tools/build_app.sh apps/basic  build/pcw/BASIC.RAW
	PCW=1 bash tools/build_engine.sh build/pcw/BASRUN2.BIN
	APPDEFS="-DGB_PCW -DBUILTIN_OFF" NOGBWIN=1 DATA_LOC=0x7F40 bash tools/build_app.sh apps/basrun build/pcw/BASRUN.RAW

dsk-pcw: raws-pcw
	bash tools/build_pcw_dsk.sh

pcw: dsk-pcw

# --- dev ----------------------------------------------------------------------
spike:
	bash tools/build_app.sh tools/spike build/SPIKE.RAW

qa-cpc: cpc
	bash tools/run_cpc.sh

qa-msx: msx
	bash tools/run_msx.sh

clean:
	rm -rf build dist
