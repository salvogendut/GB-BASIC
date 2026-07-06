# GB-BASIC — a GW-BASIC-style BASIC for GEOBENCH (CPC + MSX2).
# Deliverables: dist/GBBASIC.DSK (CPC drive-B data disk) and dist/GBBASIC-MSX.DSK.
GEOBENCH ?= ../geobench
export GEOBENCH

.PHONY: all cpc msx spike raws raws-msx dsk-cpc dsk-msx qa-cpc qa-msx clean

all: cpc msx

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

# --- dev ----------------------------------------------------------------------
spike:
	bash tools/build_app.sh tools/spike build/SPIKE.RAW

qa-cpc: cpc
	bash tools/run_cpc.sh

qa-msx: msx
	bash tools/run_msx.sh

clean:
	rm -rf build dist
