# SGDK project Makefile.
#
# SGDK's makefile.gen does all the work: it discovers src/**/*.c, res/**/*.res,
# runs rescomp, compiles for m68k, links, and emits out/rom.bin (the ELF is
# out/release/rom.out and a symbol map out/release/symbol.txt).
#
# Usage:
#   export GDK=/opt/sgdk          # or wherever SGDK lives (docker image sets this)
#   make            -> out/rom.bin        (release: -O3 -flto)
#   make debug      -> out/rom.bin        (-O1 -ggdb, asserts, KDebug/KLog)
#   make asm        -> release + .lst assembly listings
#   make clean      -> wipe out/   (do this after changing a shared struct layout
#                                   or any SGDK header: makefile.gen tracks your
#                                   own headers but not $GDK/inc, nor headers
#                                   newly nested inside other headers, and stale
#                                   objects built against an old layout cause
#                                   "impossible" crashes)
#
# Inject a define for one build (the flag below is appended, not replaced):
#   make EXTRA_FLAGS=-DSELFTEST

GDK ?= /opt/sgdk

# Must be set BEFORE the include: makefile.gen folds EXTRA_FLAGS into
# DEFAULT_FLAGS with a simply-expanded (:=) assignment, so a later value is
# silently ignored.
#
# -fno-store-merging: GCC (verified on 13.3, -O3 -m68000) merges adjacent byte
# stores into one word store, e.g. two u8 struct fields at offsets 11 and 12
# become `move.w ...,11(a0)` — a word access at an ODD address, which is an
# address error on the 68000. Cheap insurance for every project.
# `override ... +=` appends even when EXTRA_FLAGS comes from the command line.
override EXTRA_FLAGS += -fno-store-merging

include $(GDK)/makefile.gen

# Safe to set AFTER the include (used inside the link recipe, expanded late).
# - SGDK's own lib/libgcc.a is built for the 68000. On Linux, common.mk
#   defaults to -lgcc, which with Debian/Ubuntu's gcc-m68k-linux-gnu is a
#   68020+ build: its __divsi3 uses instructions that trap on a 68000.
# - --build-id=none: Linux-target binutils emit .note.gnu.build-id at address 0,
#   colliding with the 68000 vector table. Harmless with a real m68k-elf toolchain.
LIBGCC := $(GDK)/lib/libgcc.a -Wl,--build-id=none
