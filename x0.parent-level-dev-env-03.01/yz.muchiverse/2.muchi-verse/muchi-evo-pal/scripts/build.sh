#!/bin/bash
# scripts/build.sh - compile everything, warning-free.
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$SCRIPT_DIR"

mkdir -p ops/+x

CFLAGS="-Wall -Wextra -O2"

echo "--- Building prisc+x ---"
gcc $CFLAGS "system/prisc+x.c" -o "system/prisc+x"

echo "--- Building ops ---"
for src in ops/*.c; do
    name="$(basename "$src" .c)"
    echo "  Compiling $name..."
    gcc $CFLAGS "$src" -o "ops/+x/$name.+x"
done

echo "--- Build Complete ---"
ls -l system/prisc+x ops/+x/
