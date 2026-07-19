#!/bin/bash
# scripts/build.sh - compile everything, warning-free, matching
# mutaclsym's standing bar (see its dox/01-cdda-architecture.md §8).
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$SCRIPT_DIR"

mkdir -p system ops/+x

CFLAGS="-Wall -Wextra -O2"

echo "--- Building system processes ---"
gcc $CFLAGS "system/prisc+x.c" -o "system/prisc+x"
gcc $CFLAGS "system/keyboard_input.c" -o "system/keyboard_input"
gcc $CFLAGS "system/renderer.c" -o "system/renderer"

echo "--- Building ops ---"
for src in ops/*.c; do
    name="$(basename "$src" .c)"
    echo "  Compiling $name..."
    gcc $CFLAGS "$src" -o "ops/+x/$name.+x"
done

echo "--- Build Complete ---"
ls -l system/prisc+x system/keyboard_input system/renderer ops/+x/
