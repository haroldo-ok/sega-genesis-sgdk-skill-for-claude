# SGDK project Makefile.
#
# SGDK's makefile.gen does all the work: it discovers src/**/*.c, res/**/*.res,
# runs rescomp, compiles for m68k, links, and emits out/rom.bin.
#
# Usage:
#   export GDK=/opt/sgdk          # or wherever SGDK lives (docker image sets this)
#   make            -> out/rom.bin        (release: -O3 -flto)
#   make debug      -> out/rom.bin        (-O1 -ggdb, asserts, KDebug/KLog)
#   make asm        -> release + .lst assembly listings
#   make clean      -> wipe out/
#
# Override the SGDK location or inject a define:
#   make GDK=/path/to/sgdk
#   make EXTRA_FLAGS=-DSELFTEST        # e.g. an on-target self-test build

GDK ?= /opt/sgdk

include $(GDK)/makefile.gen
