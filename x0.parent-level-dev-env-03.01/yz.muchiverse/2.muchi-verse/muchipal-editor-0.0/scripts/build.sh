#!/bin/sh
# Builds every binary independently - no shared object files, no shared
# headers, matching cdda-tpm-std-fast.txt sec. 1 (same convention as
# mutaclsym/scripts/build.sh). Each translation unit is compiled and
# linked on its own line.
#
# LOCAL COPIES, NOT A LIVE SHARED_OPS REFERENCE: this project keeps its
# own real, local copy of every file below that also exists in
# yz.muchiverse/2.muchi-verse/shared-ops/ (system/prisc+x.c,
# system/keyboard_input.c, system/chtpm_parser_pal.c, ops/pdl_reader.c)
# - direct user instruction: "i dont wanna use shared ops, in the
# classic sense... all code should be self independant and solo
# shippable." This build.sh never reaches outside this project's own
# directory. To pull in an update from the canonical source, run (from
# yz.muchiverse/2.muchi-verse/):
#   bash sync_shared_op.sh <op_name> <target_dir>
# (see that directory's own shared-ops-manifest.txt for available
# op_name values) - a deliberate, explicit step, never automatic at
# build time. This project's own former ops/piece_viewer.c - a third
# independent retyping of pdl_reader.c's own parse_method_line() - is
# retired in favor of pdl_reader.c's own `list_methods_full` action
# (absolute-path input, "name|handler" output), the exact shape
# piece_viewer.c used to produce.
set -e
cd "$(dirname "$0")/.."

CC=${CC:-gcc}
CFLAGS="-std=c11 -Wall -Wextra -O2"

echo "-- prisc+x (VM)"
$CC $CFLAGS -o system/prisc+x system/prisc+x.c

echo "-- keyboard_input (raw termios, no ncurses - local copy, see"
echo "   system/keyboard_input.c's own header comment)"
$CC $CFLAGS -o system/keyboard_input system/keyboard_input.c

echo "-- renderer (plain stdout, no ncurses)"
$CC $CFLAGS -o system/renderer system/renderer.c

echo "-- ops"
$CC $CFLAGS -o ops/+x/compose_title_frame.+x ops/compose_title_frame.c
$CC $CFLAGS -o ops/+x/project_browser.+x ops/project_browser.c
$CC $CFLAGS -o ops/+x/map_edit_input.+x ops/map_edit_input.c
echo "-- local copy of formerly-shared pdl_reader (see this file's own"
echo "   header comment - sync_shared_op.sh propagates canonical"
echo "   updates here)"
$CC $CFLAGS -o ops/+x/pdl_reader.+x ops/pdl_reader.c

echo "-- chtpm_parser_pal (PERSISTENT process, not a one-shot op - same"
echo "   category as system/renderer.c/gl_mirror.c, see"
echo "   chtpm-to-pal-layout-plan.txt and this file's own header comment"
echo "   for the why/what. -Wno-unused-result -Wno-stringop-truncation"
echo "   are REQUIRED on this one file - confirmed via a real test build"
echo "   this gets to zero warnings; every other file in this project"
echo "   still builds under plain -Wall -Wextra.)"
$CC $CFLAGS -Wno-unused-result -Wno-stringop-truncation -o system/chtpm_parser_pal system/chtpm_parser_pal.c

echo "build ok"
