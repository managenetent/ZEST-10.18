#!/bin/sh
# Builds every binary independently - no shared object files, no shared
# headers, matching cdda-tpm-std-fast.txt sec. 1 (same rule mutaclsym/
# muchipal-editor already follow).
set -e
cd "$(dirname "$0")/.."

CC=${CC:-gcc}
CFLAGS="-std=c11 -Wall -Wextra -O2"

echo "-- prisc+x (VM)"
$CC $CFLAGS -o system/prisc+x system/prisc+x.c

echo "-- keyboard_input (raw termios, no ncurses)"
$CC $CFLAGS -o system/keyboard_input system/keyboard_input.c

echo "-- renderer (plain stdout, no ncurses)"
$CC $CFLAGS -o system/renderer system/renderer.c

echo "-- gl_mirror (optional GL/GLUT reader - only file allowed to call"
echo "   GL primitives, see GOVERNING CONSTRAINT in"
echo "   2.muchi-verse/GRAND-ARCHITECTURE.md - built best-effort so a"
echo "   machine without GLUT dev headers/libs can still build the rest)"
if $CC $CFLAGS -o system/gl_mirror system/gl_mirror.c -lglut -lGL -lGLU 2>/tmp/gl_mirror_build.log; then
    echo "   ok"
else
    echo "   skipped (GLUT/GL not available - see /tmp/gl_mirror_build.log)"
    rm -f /tmp/gl_mirror_build.log
fi

echo "-- ops"
$CC $CFLAGS -o ops/+x/compose_rgb_frame.+x ops/compose_rgb_frame.c -lm
$CC $CFLAGS -o ops/+x/camera_input.+x ops/camera_input.c -lm
echo "-- dump_rgb_png (DEBUG TOOL, not wired into pal/main_loop.pal or"
echo "   default_op.txt - run manually to view the RGB mirror's actual"
echo "   pixels as a real PNG)"
$CC $CFLAGS -Ilibraries -o ops/+x/dump_rgb_png.+x ops/dump_rgb_png.c -lm

echo "build ok"
